// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package binutils

import (
	"bufio"
	"fmt"
	"io"
	"os/exec"
	"strconv"
	"strings"

	"internal/plugin"
)

const (
	defaultAddr2line = "addr2line"

	// addr2line may produce multiple lines of output. We
	// use this sentinel to identify the end of the output.
	sentinel = ^uint64(0)
)

// Addr2Liner is a connection to an addr2line command for obtaining
// address and line number information from a binary.
type addr2Liner struct {
	filename string
	cmd      *exec.Cmd
	in       io.WriteCloser
	out      *bufio.Reader
	err      error

	base uint64
}

// newAddr2liner starts the given addr2liner command reporting
// information about the given executable file. If file is a shared
// library, base should be the address at which is was mapped in the
// program under consideration.
func newAddr2Liner(cmd, file string, base uint64) (*addr2Liner, error) {
	if cmd == "" {
		cmd = defaultAddr2line
	}

	a := &addr2Liner{
		filename: file,
		base:     base,
		cmd:      exec.Command(cmd, "-aif", "-e", file),
	}

	var err error
	if a.in, err = a.cmd.StdinPipe(); err != nil {
		return nil, err
	}

	outPipe, err := a.cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}

	a.out = bufio.NewReader(outPipe)
	if err := a.cmd.Start(); err != nil {
		return nil, err
	}
	return a, nil
}

// close releases any resources used by the addr2liner object.
func (d *addr2Liner) close() {
	d.in.Close()
	d.cmd.Wait()
}

func (d *addr2Liner) readString() (s string) {
	if d.err != nil {
		return ""
	}
	if s, d.err = d.out.ReadString('\n'); d.err != nil {
		return ""
	}
	return strings.TrimSpace(s)
}

// readFrame parses the addr2line output for a single address. It
// returns a populated plugin.Frame and whether it has reached the end of the
// data.
func (d *addr2Liner) readFrame() (plugin.Frame, bool) {
	funcname := d.readString()

	if strings.HasPrefix(funcname, "0x") {
		// If addr2line returns a hex address we can assume it is the
		// sentinel.  Read and ignore next two lines of output from
		// addr2line
		d.readString()
		d.readString()
		return plugin.Frame{}, true
	}

	fileline := d.readString()
	if d.err != nil {
		return plugin.Frame{}, true
	}

	linenumber := 0

	if funcname == "??" {
		funcname = ""
	}

	if fileline == "??:0" {
		fileline = ""
	} else {
		if i := strings.LastIndex(fileline, ":"); i >= 0 {
			// Remove discriminator, if present
			if disc := strings.Index(fileline, " (discriminator"); disc > 0 {
				fileline = fileline[:disc]
			}
			// If we cannot parse a number after the last ":", keep it as
			// part of the filename.
			if line, err := strconv.Atoi(fileline[i+1:]); err == nil {
				linenumber = line
				fileline = fileline[:i]
			}
		}
	}

	return plugin.Frame{funcname, fileline, linenumber}, false
}

// addrInfo returns the stack frame information for a specific program
// address. It returns nil if the address could not be identified.
func (d *addr2Liner) addrInfo(addr uint64) ([]plugin.Frame, error) {
	if d.err != nil {
		return nil, d.err
	}

	if _, d.err = fmt.Fprintf(d.in, "%x\n", addr-d.base); d.err != nil {
		return nil, d.err
	}

	if _, d.err = fmt.Fprintf(d.in, "%x\n", sentinel); d.err != nil {
		return nil, d.err
	}

	resp := d.readString()
	if d.err != nil {
		return nil, d.err
	}

	if !strings.HasPrefix(resp, "0x") {
		d.err = fmt.Errorf("unexpected addr2line output: %s", resp)
		return nil, d.err
	}

	var stack []plugin.Frame
	for {
		frame, end := d.readFrame()
		if end {
			break
		}

		if frame != (plugin.Frame{}) {
			stack = append(stack, frame)
		}
	}
	return stack, d.err
}
