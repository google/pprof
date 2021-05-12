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
	"regexp"
	"strconv"
	"strings"
	"sync"

	"github.com/google/pprof/internal/plugin"
)

const (
	defaultLLVMGsymUtil = "llvm-gsymutil"
)

// llvmGsymUtil is a connection to an llvm-symbolizer command for
// obtaining address and line number information from a binary.
type llvmGsymUtil struct {
	sync.Mutex
	filename string
	rw       lineReaderWriter
	base     uint64
}

type llvmGsymUtilJob struct {
	cmd *exec.Cmd
	in  io.WriteCloser
	out *bufio.Reader
}

func (a *llvmGsymUtilJob) write(s string) error {
	_, err := fmt.Fprintln(a.in, s)
	return err
}

func (a *llvmGsymUtilJob) readLine() (string, error) {
	s, err := a.out.ReadString('\n')
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(s), nil
}

// close releases any resources used by the llvmGsymUtil object.
func (a *llvmGsymUtilJob) close() {
	a.in.Close()
	a.cmd.Wait()
}

// newLLVMGsymUtil starts the given llvmGsymUtil command reporting
// information about the given executable file. If file is a shared
// library, base should be the address at which it was mapped in the
// program under consideration.
func newLLVMGsymUtil(cmd, file string, base uint64, isData bool) (*llvmGsymUtil, error) {
	if cmd == "" {
		cmd = defaultLLVMGsymUtil
	}

	j := &llvmGsymUtilJob{
		cmd: exec.Command(cmd, "--addresses-from-stdin"),
	}

	var err error
	if j.in, err = j.cmd.StdinPipe(); err != nil {
		return nil, err
	}

	outPipe, err := j.cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}

	j.out = bufio.NewReader(outPipe)
	if err := j.cmd.Start(); err != nil {
		return nil, err
	}

	a := &llvmGsymUtil{
		filename: file,
		rw:       j,
		base:     base,
	}

	return a, nil
}

// readFrame parses the llvm-symbolizer output for a single address. It
// returns a populated plugin.Frame and whether it has reached the end of the
// data.
func (d *llvmGsymUtil) readFrame() (plugin.Frame, bool) {
	line, err := d.rw.readLine()
	if err != nil || len(line) == 0 {
		return plugin.Frame{}, true
	}

	print("read line: " + line + "\n")

	// TODO compile regexes once
	prefixRegex := regexp.MustCompile(`^(0x[[:xdigit:]]+:\s|\s+)`)
	// _ZNK2sf12RefCountBaseILb0EE9removeRefEv + 3 @ /home/sgiesecke/Snowflake/trunk/ExecPlatform/build/ReleaseClangLTO/../../src/core/ptr/RefCountBase.hpp:67 [inlined]
	frameRegex := regexp.MustCompile(`(\S+).* @ (.*):([[:digit:]]+)`)

	// The first frame contains an address: prefix. We don't need that. The remaining frames start with spaces.
	suffix := prefixRegex.ReplaceAllString(line, "")

	print("suffix is: " + suffix + "\n")

	if strings.HasPrefix(suffix, "error:") {
		// Skip empty line that follows.
		_, _ = d.rw.readLine()
		return plugin.Frame{}, true
	}

	frameMatch := frameRegex.FindStringSubmatch(suffix)
	if frameMatch == nil {
		print("frame regex didn't match\n")

		return plugin.Frame{}, true
	}

	// TODO handle cases where no source file/line is available
	// TODO handle column number?

	funcname := frameMatch[1]
	sourceFile := frameMatch[2]
	sourceLineStr := frameMatch[3]

	sourceLine := 0
	if line, err := strconv.Atoi(sourceLineStr); err == nil {
		sourceLine = line
	}

	return plugin.Frame{Func: funcname, File: sourceFile, Line: sourceLine}, false
}

// addrInfo returns the stack frame information for a specific program
// address. It returns nil if the address could not be identified.
func (d *llvmGsymUtil) addrInfo(addr uint64) ([]plugin.Frame, error) {
	d.Lock()
	defer d.Unlock()

	print("querying gsym: " + fmt.Sprintf("0x%x %s.gsym", addr-d.base, d.filename) + "\n")

	if err := d.rw.write(fmt.Sprintf("0x%x %s.gsym", addr-d.base, d.filename)); err != nil {
		return nil, err
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

	return stack, nil
}
