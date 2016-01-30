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
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/report"
	"github.com/google/pprof/profile"
	"github.com/google/pprof/third_party/svg"
)

// commands describes the commands accepted by pprof.
type commands map[string]*command

// command describes the actions for a pprof command. Includes a
// function for command-line completion, the report format to use
// during report generation, any postprocessing functions, and whether
// the command expects a regexp parameter (typically a function name).
type command struct {
	format      int           // report format to generate
	postProcess PostProcessor // postprocessing to run on report
	hasParam    bool          // collect a parameter from the CLI
	description string        // single-line description text saying what the command does
	usage       string        // multi-line help text saying how the command is used
}

// help returns a help string for a command.
func (c *command) help(name string) string {
	message := c.description + "\n"
	if c.usage != "" {
		message += "  Usage:\n"
		lines := strings.Split(c.usage, "\n")
		for _, line := range lines {
			message += fmt.Sprintf("    %s\n", line)
		}
	}
	return message + "\n"
}

func AddCommand(cmd string, format int, post PostProcessor, desc, usage string) {
	pprofCommands[cmd] = &command{format, post, false, desc, usage}
}

// PostProcessor is a function that applies post-processing to the report output
type PostProcessor func(input []byte, output io.Writer, ui plugin.UI) error

// WaitForVisualizer makes pprof wait for visualizers to complete
// before continuing, returning any errors.
var waitForVisualizer = true

// pprofCommands are the report generation commands recognized by pprof.
var pprofCommands = commands{
	// Commands that require no post-processing.
	"tags":     {report.Tags, nil, false, "Outputs all tags in the profile", "tags [tag_regex]* [-ignore_regex]* [>file]\nList tags with key:value matching tag_regex and exclude ignore_regex."},
	"raw":      {report.Raw, nil, false, "Outputs a text representation of the raw profile", ""},
	"dot":      {report.Dot, nil, false, "Outputs a graph in DOT format", reportHelp("dot", false, true)},
	"top":      {report.Text, nil, false, "Outputs top entries in text form", reportHelp("top", true, true)},
	"tree":     {report.Tree, nil, false, "Outputs a text rendering of call graph", reportHelp("tree", true, true)},
	"text":     {report.Text, nil, false, "Outputs top entries in text form", reportHelp("text", true, true)},
	"traces":   {report.Traces, nil, false, "Outputs all profile samples in text form", ""},
	"topproto": {report.TopProto, awayFromTTY("pb.gz"), false, "Outputs top entries in compressed protobuf format", ""},
	"disasm":   {report.Dis, nil, true, "Output assembly listings annotated with samples", listHelp("disasm", true)},
	"list":     {report.List, nil, true, "Output annotated source for functions matching regexp", listHelp("list", false)},
	"peek":     {report.Tree, nil, true, "Output callers/callees of functions matching regexp", "peek func_regex\nDisplay callers and callees of functions matching func_regex."},

	// Save binary formats to a file
	"callgrind": {report.Callgrind, awayFromTTY("callgraph.out"), false, "Outputs a graph in callgrind format", reportHelp("callgrind", false, true)},
	"proto":     {report.Proto, awayFromTTY("pb.gz"), false, "Outputs the profile in compressed protobuf format", ""},

	// Generate report in DOT format and postprocess with dot
	"gif": {report.Dot, invokeDot("gif"), false, "Outputs a graph image in GIF format", reportHelp("gif", false, true)},
	"pdf": {report.Dot, invokeDot("pdf"), false, "Outputs a graph in PDF format", reportHelp("pdf", false, true)},
	"png": {report.Dot, invokeDot("png"), false, "Outputs a graph image in PNG format", reportHelp("png", false, true)},
	"ps":  {report.Dot, invokeDot("ps"), false, "Outputs a graph in PS format", reportHelp("ps", false, true)},

	// Save SVG output into a file
	"svg": {report.Dot, saveSVGToFile(), false, "Outputs a graph in SVG format", reportHelp("svg", false, true)},

	// Visualize postprocessed dot output
	"eog":    {report.Dot, invokeVisualizer(invokeDot("svg"), "svg", []string{"eog"}), false, "Visualize graph through eog", reportHelp("eog", false, false)},
	"evince": {report.Dot, invokeVisualizer(invokeDot("pdf"), "pdf", []string{"evince"}), false, "Visualize graph through evince", reportHelp("evince", false, false)},
	"gv":     {report.Dot, invokeVisualizer(invokeDot("ps"), "ps", []string{"gv --noantialias"}), false, "Visualize graph through gv", reportHelp("gv", false, false)},
	"web":    {report.Dot, invokeVisualizer(saveSVGToFile(), "svg", browsers()), false, "Visualize graph through web browser", reportHelp("web", false, false)},

	// Visualize callgrind output
	"kcachegrind": {report.Callgrind, invokeVisualizer(nil, "grind", kcachegrind), false, "Visualize report in KCachegrind", reportHelp("kcachegrind", false, false)},

	// Visualize HTML directly generated by report.
	"weblist": {report.WebList, invokeVisualizer(awayFromTTY("html"), "html", browsers()), true, "Display annotated source in a web browser", listHelp("weblist", false)},
}

// pprofVariables are the configuration parameters that affect the
// reported generated by pprof.
var pprofVariables = variables{
	// Filename for file-based output formats, stdout by default.
	"output": &variable{stringKind, "", "", helpText("Output filename for file-based outputs")},

	// Comparisons.
	"drop_negative": &variable{boolKind, "f", "", helpText(
		"Ignore negative differences",
		"Do not show any locations with values <0.")},

	// Comparisons.
	"positive_percentages": &variable{boolKind, "f", "", helpText(
		"Ignore negative samples when computing percentages",
		" Do not count negative samples when computing the total value",
		" of the profile, used to compute percentages. If set, and the -base",
		" option is used, percentages reported will be computed against the",
		" main profile, ignoring the base profile.")},

	// Graph handling options.
	"call_tree": &variable{boolKind, "f", "", helpText(
		"Create a context-sensitive call tree",
		"Treat locations reached through different paths as separate.")},

	// Display options.
	"relative_percentages": &variable{boolKind, "f", "", helpText(
		"Show percentages relative to focused subgraph",
		"If unset, percentages are relative to full graph before focusing",
		"to facilitate comparison with original graph.")},
	"unit": &variable{stringKind, "minimum", "", helpText(
		"Measurement units to display",
		"Scale the sample values to this unit.",
		" For time-based profiles, use seconds, milliseconds, nanoseconds, etc.",
		" For memory profiles, use megabytes, kiloyes, bytes, etc.",
		" auto will scale each value independently to the most natural unit.")},
	"compact_labels": &variable{boolKind, "f", "", "Show minimal headers"},

	// Filtering options
	"nodecount": &variable{intKind, "-1", "", helpText(
		"Max number of nodes to show",
		"Uses heuristics to limit the number of locations to be displayed.",
		"On graphs, dotted edges represent paths through nodes that have been removed.")},
	"nodefraction": &variable{floatKind, "0.005", "", "Hide nodes below <f>*total"},
	"edgefraction": &variable{floatKind, "0.001", "", "Hide edges below <f>*total"},
	"trim": &variable{boolKind, "t", "", helpText(
		"Honor nodefraction/edgefraction/nodecount defaults",
		"Set to false to get the full profile, without any trimming.")},
	"focus": &variable{stringKind, "", "", helpText(
		"Restricts to samples going through a node matching regexp",
		"Discard samples that do not include a node matching this regexp.",
		"Matching includes the function name, filename or object name.")},
	"ignore": &variable{stringKind, "", "", helpText(
		"Skips paths going through any nodes matching regexp",
		"If set, discard samples that include a node matching this regexp.",
		"Matching includes the function name, filename or object name.")},
	"prune_from": &variable{stringKind, "", "", helpText(
		"Drops any functions below the matched frame.",
		"If set, any frames matching the specified regexp and any frames",
		"below it will be dropped from each sample.")},
	"hide": &variable{stringKind, "", "", helpText(
		"Skips nodes matching regexp",
		"Discard nodes that match this location.",
		"Other nodes from samples that include this location will be shown.",
		"Matching includes the function name, filename or object name.")},
	"show": &variable{stringKind, "", "", helpText(
		"Only show nodes matching regexp",
		"If set, only show nodes that match this location.",
		"Matching includes the function name, filename or object name.")},
	"tagfocus": &variable{stringKind, "", "", helpText(
		"Restrict to samples with tags in range or matched by regexp",
		"Discard samples that do not include a node with a tag matching this regexp.")},
	"tagignore": &variable{stringKind, "", "", helpText(
		"Discard samples with tags in range or matched by regexp",
		"Discard samples that do include a node with a tag matching this regexp.")},

	// Heap profile options
	"divide_by": &variable{floatKind, "1", "", helpText(
		"Ratio to divide all samples before visualization",
		"Divide all samples values by a constant, eg the number of processors or jobs.")},
	"mean": &variable{boolKind, "f", "", helpText(
		"Average sample value over first value (count)",
		"For memory profiles, report average memory per allocation.",
		"For time-based profiles, report average time per event.")},
	"sample_index": &variable{stringKind, "", "", helpText(
		"Sample value to report",
		"Profiles contain multiple values per sample.",
		"Use sample_value=index to select the ith value or select it by name.")},

	// Data sorting criteria
	"flat": &variable{boolKind, "t", "cumulative", helpText("Sort entries based on own weight")},
	"cum":  &variable{boolKind, "f", "cumulative", helpText("Sort entries based on cumulative weight")},

	// Output granularity
	"functions": &variable{boolKind, "t", "granularity", helpText(
		"Aggregate at the function level.",
		"Takes into account the filename/lineno where the function was defined.")},
	"functionnameonly": &variable{boolKind, "f", "granularity", helpText(
		"Aggregate at the function level.",
		"Ignores the filename/lineno where the function was defined.")},
	"files": &variable{boolKind, "f", "granularity", "Aggregate at the file level."},
	"lines": &variable{boolKind, "f", "granularity", "Aggregate at the source code line level."},
	"addresses": &variable{boolKind, "f", "granularity", helpText(
		"Aggregate at the function level.",
		"Includes functions' addresses in the output.")},
	"noinlines": &variable{boolKind, "f", "granularity", helpText(
		"Aggregate at the function level.",
		"Attributes inlined functions to their first out-of-line caller.")},
	"addressnoinlines": &variable{boolKind, "f", "granularity", helpText(
		"Aggregate at the function level, including functions' addresses in the output.",
		"Attributes inlined functions to their first out-of-line caller.")},
}

func helpText(s ...string) string {
	return strings.Join(s, "\n") + "\n"
}

// usage returns a string describing the pprof commands and variables.
// if commandLine is set, the output reflect cli usage.
func usage(commandLine bool) string {
	var prefix string
	if commandLine {
		prefix = "-"
	}
	fmtHelp := func(c, d string) string {
		return fmt.Sprintf("    %-16s %s", c, strings.SplitN(d, "\n", 2)[0])
	}

	var commands []string
	for name, cmd := range pprofCommands {
		commands = append(commands, fmtHelp(prefix+name, cmd.description))
	}
	sort.Strings(commands)

	var help string
	if commandLine {
		help = "  Output formats (select only one):\n"
	} else {
		help = "  Commands:\n"
		commands = append(commands, fmtHelp("quit/exit/^D", "Exit pprof"))
	}

	help = help + strings.Join(commands, "\n") + "\n\n" +
		"  Options:\n"

	// Print help for variables after sorting them.
	// Collect radio variables by their group name to print them together.
	radioOptions := make(map[string][]string)
	var variables []string
	for name, vr := range pprofVariables {
		if vr.group != "" {
			radioOptions[vr.group] = append(radioOptions[vr.group], name)
			continue
		}
		variables = append(variables, fmtHelp(prefix+name, vr.help))
	}
	sort.Strings(variables)

	help = help + strings.Join(variables, "\n") + "\n\n" +
		"  Option groups (only set one per group):\n"

	var radioStrings []string
	for radio, ops := range radioOptions {
		sort.Strings(ops)
		s := []string{fmtHelp(radio, "")}
		for _, op := range ops {
			s = append(s, "  "+fmtHelp(prefix+op, pprofVariables[op].help))
		}

		radioStrings = append(radioStrings, strings.Join(s, "\n"))
	}
	sort.Strings(radioStrings)
	return help + strings.Join(radioStrings, "\n")
}

func reportHelp(c string, cum, redirect bool) string {
	h := []string{
		c + " [n] [focus_regex]* [-ignore_regex]*",
		"Include up to n samples",
		"Include samples matching focus_regex, and exclude ignore_regex.",
	}
	if cum {
		h[0] += " [-cum]"
		h = append(h, "-cum sorts the output by cumulative weight")
	}
	if redirect {
		h[0] += " >f"
		h = append(h, "Optionally save the report on the file f")
	}
	return strings.Join(h, "\n")
}

func listHelp(c string, redirect bool) string {
	h := []string{
		c + "<func_regex|address> [-focus_regex]* [-ignore_regex]*",
		"Include functions matching func_regex, or including the address specified.",
		"Include samples matching focus_regex, and exclude ignore_regex.",
	}
	if redirect {
		h[0] += " >f"
		h = append(h, "Optionally save the report on the file f")
	}
	return strings.Join(h, "\n")
}

// browsers returns a list of commands to attempt for web visualization.
func browsers() []string {
	cmds := []string{"chrome", "google-chrome", "firefox"}
	switch runtime.GOOS {
	case "darwin":
		return append(cmds, "/usr/bin/open")
	case "windows":
		return append(cmds, "cmd /c start")
	default:
		user_browser := os.Getenv("BROWSER")
		if user_browser != "" {
			cmds = append([]string{user_browser, "sensible-browser"}, cmds...)
		} else {
			cmds = append([]string{"sensible-browser"}, cmds...)
		}
		return append(cmds, "xdg-open")
	}
}

var kcachegrind = []string{"kcachegrind"}

// awayFromTTY saves the output in a file if it would otherwise go to
// the terminal screen. This is used to avoid dumping binary data on
// the screen.
func awayFromTTY(format string) PostProcessor {
	return func(input []byte, output io.Writer, ui plugin.UI) error {
		if output == os.Stdout && ui.IsTerminal() {
			tempFile, err := newTempFile("", "profile", "."+format)
			if err != nil {
				return err
			}
			ui.PrintErr("Generating report in ", tempFile.Name())
			_, err = fmt.Fprint(tempFile, string(input))
			return err
		}
		_, err := fmt.Fprint(output, string(input))
		return err
	}
}

func invokeDot(format string) PostProcessor {
	divert := awayFromTTY(format)
	return func(input []byte, output io.Writer, ui plugin.UI) error {
		cmd := exec.Command("dot", "-T"+format)
		var buf bytes.Buffer
		cmd.Stdin, cmd.Stdout, cmd.Stderr = bytes.NewBuffer(input), &buf, os.Stderr
		if err := cmd.Run(); err != nil {
			return fmt.Errorf("Failed to execute dot. Is Graphviz installed? Error: %v", err)
		}
		return divert(buf.Bytes(), output, ui)
	}
}

func saveSVGToFile() PostProcessor {
	generateSVG := invokeDot("svg")
	divert := awayFromTTY("svg")
	return func(input []byte, output io.Writer, ui plugin.UI) error {
		baseSVG := &bytes.Buffer{}
		if err := generateSVG(input, baseSVG, ui); err != nil {
			return err
		}

		return divert([]byte(svg.Massage(*baseSVG)), output, ui)
	}
}

func invokeVisualizer(format PostProcessor, suffix string, visualizers []string) PostProcessor {
	return func(input []byte, output io.Writer, ui plugin.UI) error {
		if output != os.Stdout {
			if format != nil {
				return format(input, output, ui)
			}
			_, err := fmt.Fprint(output, string(input))
			return err
		}

		tempFile, err := newTempFile(os.Getenv("PPROF_TMPDIR"), "pprof", "."+suffix)
		if err != nil {
			return err
		}
		deferDeleteTempFile(tempFile.Name())
		if format != nil {
			if err := format(input, tempFile, ui); err != nil {
				return err
			}
		} else {
			if _, err := fmt.Fprint(tempFile, string(input)); err != nil {
				return err
			}
		}
		tempFile.Close()
		// Try visualizers until one is successful
		for _, v := range visualizers {
			// Separate command and arguments for exec.Command.
			args := strings.Split(v, " ")
			if len(args) == 0 {
				continue
			}
			viewer := exec.Command(args[0], append(args[1:], tempFile.Name())...)
			viewer.Stderr = os.Stderr
			if err = viewer.Start(); err == nil {
				// Wait for a second so that the visualizer has a chance to
				// open the input file. This needs to be done even if we're
				// waiting for the visualizer as it can be just a wrapper that
				// spawns a browser tab and returns right away.
				defer func(t <-chan time.Time) {
					<-t
				}(time.After(time.Second))
				if waitForVisualizer {
					return viewer.Wait()
				}
				return nil
			}
		}
		return err
	}
}

// locateSampleIndex returns the appropriate index for a value of sample index.
// If numeric, it returns the number, otherwise it looks up the text in the
// profile sample types.
func locateSampleIndex(p *profile.Profile, sampleIndex string) (int, error) {
	if sampleIndex == "" {
		// By default select the last sample value
		return len(p.SampleType) - 1, nil
	}
	if i, err := strconv.Atoi(sampleIndex); err == nil {
		if i < 0 || i >= len(p.SampleType) {
			return 0, fmt.Errorf("sample_index %s is outside the range [0..%d]", sampleIndex, len(p.SampleType)-1)
		}
		return i, nil
	}

	// Remove the inuse_ prefix to support legacy pprof options
	// "inuse_space" and "inuse_objects" for profiles containing types
	// "space" and "objects".
	noInuse := strings.TrimPrefix(sampleIndex, "inuse_")
	sampleTypes := make([]string, len(p.SampleType))
	for i, t := range p.SampleType {
		if t.Type == sampleIndex || t.Type == noInuse {
			return i, nil
		}
		sampleTypes[i] = t.Type
	}

	return 0, fmt.Errorf("sample_index %q must be one of: %v", sampleIndex, sampleTypes)
}

// variables describe the configuration parameters recognized by pprof.
type variables map[string]*variable

// variable is a single configuration parameter.
type variable struct {
	kind  int    // How to interpret the value, must be one of the enums below.
	value string // Effective value. Only values appropriate for the Kind should be set.
	group string // boolKind variables with the same Group != "" cannot be set simultaneously.
	help  string // Text describing the variable, in multiple lines separated by newline.
}

const (
	// variable.kind must be one of these variables.
	boolKind = iota
	intKind
	floatKind
	stringKind
)

// set updates the value of a variable, checking that the value is
// suitable for the variable Kind.
func (vars variables) set(name, value string) error {
	v := vars[name]
	if v == nil {
		return fmt.Errorf("no variable %s", name)
	}
	var err error
	switch v.kind {
	case boolKind:
		var b bool
		if b, err = stringToBool(value); err == nil {
			if v.group != "" && b == false {
				err = fmt.Errorf("%q can only be set to true", name)
			}
		}
	case intKind:
		_, err = strconv.Atoi(value)
	case floatKind:
		_, err = strconv.ParseFloat(value, 64)
	}
	if err != nil {
		return err
	}
	vars[name].value = value
	if group := vars[name].group; group != "" {
		for vname, vvar := range vars {
			if vvar.group == group && vname != name {
				vvar.value = "f"
			}
		}
	}
	return err
}

// boolValue returns the value of a boolean variable.
func (v *variable) boolValue() bool {
	b, err := stringToBool(v.value)
	if err != nil {
		panic("unexpected value " + v.value + " for bool ")
	}
	return b
}

// intValue returns the value of an intKind variable.
func (v *variable) intValue() int {
	i, err := strconv.Atoi(v.value)
	if err != nil {
		panic("unexpected value " + v.value + " for int ")
	}
	return i
}

// floatValue returns the value of a Float variable.
func (v *variable) floatValue() float64 {
	f, err := strconv.ParseFloat(v.value, 64)
	if err != nil {
		panic("unexpected value " + v.value + " for float ")
	}
	return f
}

// stringValue returns a canonical representation for a variable.
func (v *variable) stringValue() string {
	switch v.kind {
	case boolKind:
		return fmt.Sprint(v.boolValue())
	case intKind:
		return fmt.Sprint(v.intValue())
	case floatKind:
		return fmt.Sprint(v.floatValue())
	}
	return v.value
}

func stringToBool(s string) (bool, error) {
	switch strings.ToLower(s) {
	case "true", "t", "yes", "y", "1", "":
		return true, nil
	case "false", "f", "no", "n", "0":
		return false, nil
	default:
		return false, fmt.Errorf(`illegal value "%s" for bool variable`, s)
	}
}

// makeCopy returns a duplicate of a set of shell variables.
func (vars variables) makeCopy() variables {
	varscopy := make(variables, len(vars))
	for n, v := range vars {
		vcopy := *v
		varscopy[n] = &vcopy
	}
	return varscopy
}
