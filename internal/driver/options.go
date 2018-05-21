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

package driver

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/chzyer/readline"
	"github.com/google/pprof/internal/binutils"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/symbolizer"
)

// setDefaults returns a new plugin.Options with zero fields sets to
// sensible defaults.
func setDefaults(o *plugin.Options) *plugin.Options {
	d := &plugin.Options{}
	if o != nil {
		*d = *o
	}
	if d.Writer == nil {
		d.Writer = oswriter{}
	}
	if d.Flagset == nil {
		d.Flagset = goFlags{}
	}
	if d.Obj == nil {
		d.Obj = &binutils.Binutils{}
	}
	if d.UI == nil {
		d.UI = defaultUI()
	}
	if d.Sym == nil {
		d.Sym = &symbolizer.Symbolizer{Obj: d.Obj, UI: d.UI}
	}
	return d
}

// goFlags returns a flagset implementation based on the standard flag
// package from the Go distribution. It implements the plugin.FlagSet
// interface.
type goFlags struct{}

func (goFlags) Bool(o string, d bool, c string) *bool {
	return flag.Bool(o, d, c)
}

func (goFlags) Int(o string, d int, c string) *int {
	return flag.Int(o, d, c)
}

func (goFlags) Float64(o string, d float64, c string) *float64 {
	return flag.Float64(o, d, c)
}

func (goFlags) String(o, d, c string) *string {
	return flag.String(o, d, c)
}

func (goFlags) BoolVar(b *bool, o string, d bool, c string) {
	flag.BoolVar(b, o, d, c)
}

func (goFlags) IntVar(i *int, o string, d int, c string) {
	flag.IntVar(i, o, d, c)
}

func (goFlags) Float64Var(f *float64, o string, d float64, c string) {
	flag.Float64Var(f, o, d, c)
}

func (goFlags) StringVar(s *string, o, d, c string) {
	flag.StringVar(s, o, d, c)
}

func (goFlags) StringList(o, d, c string) *[]*string {
	return &[]*string{flag.String(o, d, c)}
}

func (goFlags) ExtraUsage() string {
	return ""
}

func (goFlags) Parse(usage func()) []string {
	flag.Usage = usage
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		usage()
	}
	return args
}

func defaultUI() plugin.UI {
	rl, err := readline.New("")
	if err != nil {
		fmt.Fprintf(os.Stderr, "fall back to the default UI due to a failure in initializing readline: %v", err)
		return &stdUI{r: bufio.NewReader(os.Stdin)}
	}
	return &readlineUI{rl: rl}
}

type stdUI struct {
	r *bufio.Reader
}

func (ui *stdUI) ReadLine(prompt string) (string, error) {
	os.Stdout.WriteString(prompt)
	return ui.r.ReadString('\n')
}

func (ui *stdUI) Print(args ...interface{}) {
	ui.fprint(os.Stderr, args)
}

func (ui *stdUI) PrintErr(args ...interface{}) {
	ui.fprint(os.Stderr, args)
}

func (ui *stdUI) IsTerminal() bool {
	return false
}

func (ui *stdUI) WantBrowser() bool {
	return true
}

func (ui *stdUI) SetAutoComplete(func(string) string) {
}

func (ui *stdUI) fprint(f *os.File, args []interface{}) {
	text := fmt.Sprint(args...)
	if !strings.HasSuffix(text, "\n") {
		text += "\n"
	}
	f.WriteString(text)
}

// readlineUI implements the driver.UI interface using the
// github.com/chzyer/readline library.
// This is contained in pprof.go to avoid adding the readline
// dependency in the vendored copy of pprof in the Go distribution,
// which does not use this file.
type readlineUI struct {
	rl *readline.Instance
}

// Read returns a line of text (a command) read from the user.
// prompt is printed before reading the command.
func (r *readlineUI) ReadLine(prompt string) (string, error) {
	r.rl.SetPrompt(prompt)
	return r.rl.Readline()
}

// Print shows a message to the user.
// It is printed over stderr as stdout is reserved for regular output.
func (r *readlineUI) Print(args ...interface{}) {
	text := fmt.Sprint(args...)
	if !strings.HasSuffix(text, "\n") {
		text += "\n"
	}
	fmt.Fprint(r.rl.Stderr(), text)
}

// Print shows a message to the user, colored in red for emphasis.
// It is printed over stderr as stdout is reserved for regular output.
func (r *readlineUI) PrintErr(args ...interface{}) {
	text := fmt.Sprint(args...)
	if !strings.HasSuffix(text, "\n") {
		text += "\n"
	}
	fmt.Fprint(r.rl.Stderr(), colorize(text))
}

// colorize the msg using ANSI color escapes.
func colorize(msg string) string {
	var red = 31
	var colorEscape = fmt.Sprintf("\033[0;%dm", red)
	var colorResetEscape = "\033[0m"
	return colorEscape + msg + colorResetEscape
}

// IsTerminal returns whether the UI is known to be tied to an
// interactive terminal (as opposed to being redirected to a file).
func (r *readlineUI) IsTerminal() bool {
	const stdout = 1
	return readline.IsTerminal(stdout)
}

// Start a browser on interactive mode.
func (r *readlineUI) WantBrowser() bool {
	return r.IsTerminal()
}

// SetAutoComplete instructs the UI to call complete(cmd) to obtain
// the auto-completion of cmd, if the UI supports auto-completion at all.
func (r *readlineUI) SetAutoComplete(complete func(string) string) {
	// TODO: Implement auto-completion support.
}

// oswriter implements the Writer interface using a regular file.
type oswriter struct{}

func (oswriter) Open(name string) (io.WriteCloser, error) {
	f, err := os.Create(name)
	return f, err
}
