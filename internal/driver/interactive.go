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
	"fmt"
	"io"
	"sort"
	"strconv"
	"strings"

	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/report"
	"github.com/google/pprof/profile"
)

// interactive starts a shell to read pprof commands.
func interactive(p *profile.Profile, o *plugin.Options) error {
	// Enter command processing loop.
	o.UI.SetAutoComplete(newCompleter(functionNames(p)))
	pprofVariables.set("compact_labels", "true")
	pprofVariables["sample_index"].help += fmt.Sprintf("Or use sample_index=name, with name in %v.\n", sampleTypes(p))

	// Do not wait for the visualizer to complete, to allow multiple
	// graphs to be visualized simultaneously.
	waitForVisualizer = false
	shortcuts := profileShortcuts(p)

	greetings(p, o.UI)
	for {
		input, err := o.UI.ReadLine(pprofPrompt(p))
		if err != nil {
			if err != io.EOF {
				return err
			}
			if input == "" {
				return nil
			}
		}

		for _, input := range shortcuts.expand(input) {
			// Process assignments of the form variable=value
			if s := strings.SplitN(input, "=", 2); len(s) > 0 {
				name := strings.TrimSpace(s[0])

				if v := pprofVariables[name]; v != nil {
					var value string
					if len(s) == 2 {
						value = strings.TrimSpace(s[1])
					}
					if err := pprofVariables.set(name, value); err != nil {
						o.UI.PrintErr(err)
					}
					continue
				}
			}

			tokens := strings.Fields(input)
			if len(tokens) == 0 {
				continue
			}

			switch tokens[0] {
			case "exit", "quit":
				return nil
			case "help":
				commandHelp(strings.Join(tokens[1:], " "), o.UI)
				continue
			}

			args, vars, err := parseCommandLine(tokens)
			if err == nil {
				err = generateReportWrapper(p, args, vars, o)
			}

			if err != nil {
				o.UI.PrintErr(err)
			}
		}
	}
}

var generateReportWrapper = generateReport // For testing purposes.

// greetings prints a brief welcome and some overall profile
// information before accepting interactive commands.
func greetings(p *profile.Profile, ui plugin.UI) {
	ropt, err := reportOptions(p, pprofVariables)
	if err == nil {
		ui.Print(strings.Join(report.ProfileLabels(report.New(p, ropt)), "\n"))
		ui.Print(fmt.Sprintf("Sample types: %v\n", sampleTypes(p)))
	}
	ui.Print("Entering interactive mode (type \"help\" for commands)")
}

// shortcuts represents composite commands that expand into a sequence
// of other commands.
type shortcuts map[string][]string

func (a shortcuts) expand(input string) []string {
	if a != nil {
		if r, ok := a[input]; ok {
			return r
		}
	}
	return []string{input}
}

var pprofShortcuts = shortcuts{
	":": []string{"focus=", "ignore=", "hide=", "tagfocus=", "tagignore="},
}

// profileShortcuts creates macros for convenience and backward compatibility.
func profileShortcuts(p *profile.Profile) shortcuts {
	s := pprofShortcuts
	// Add shortcuts for sample types
	for _, st := range p.SampleType {
		command := fmt.Sprintf("sample_index=%s", st.Type)
		s[st.Type] = []string{command}
		s["total_"+st.Type] = []string{"mean=0", command}
		s["mean_"+st.Type] = []string{"mean=1", command}
	}
	return s
}

// pprofPrompt returns the prompt displayed to accept commands.
// hides some default values to reduce clutter.
func pprofPrompt(p *profile.Profile) string {
	var args []string
	for n, o := range pprofVariables {
		v := o.stringValue()
		if v == "" {
			continue
		}
		// Do not show some default values.
		switch {
		case n == "unit" && v == "minimum":
			continue
		case n == "divide_by" && v == "1":
			continue
		case n == "nodecount" && v == "-1":
			continue
		case n == "sample_index":
			index, err := locateSampleIndex(p, v)
			if err != nil {
				v = "ERROR: " + err.Error()
			} else {
				v = fmt.Sprintf("%s (%d)", p.SampleType[index].Type, index)
			}
		case n == "trim" || n == "compact_labels":
			if o.boolValue() == true {
				continue
			}
		case o.kind == boolKind:
			if o.boolValue() == false {
				continue
			}
		case n == "source_path":
			continue
		}
		args = append(args, fmt.Sprintf("  %-25s : %s", n, v))
	}
	sort.Strings(args)
	return "Options:\n" + strings.Join(args, "\n") + "\nPPROF>"
}

// parseCommandLine parses a command and returns the pprof command to
// execute and a set of variables for the report.
func parseCommandLine(input []string) ([]string, variables, error) {
	cmd, args := input[:1], input[1:]
	name := cmd[0]

	c := pprofCommands[cmd[0]]
	if c == nil {
		return nil, nil, fmt.Errorf("Unrecognized command: %q", name)
	}

	if c.hasParam {
		if len(args) == 0 {
			return nil, nil, fmt.Errorf("command %s requires an argument", name)
		}
		cmd = append(cmd, args[0])
		args = args[1:]
	}

	// Copy the variables as options set in the command line are not persistent.
	vcopy := pprofVariables.makeCopy()

	var focus, ignore string
	for i := 0; i < len(args); i++ {
		t := args[i]
		if _, err := strconv.ParseInt(t, 10, 32); err == nil {
			vcopy.set("nodecount", t)
			continue
		}
		switch t[0] {
		case '>':
			outputFile := t[1:]
			if outputFile == "" {
				i++
				if i >= len(args) {
					return nil, nil, fmt.Errorf("Unexpected end of line after >")
				}
				outputFile = args[i]
			}
			vcopy.set("output", outputFile)
		case '-':
			if t == "--cum" || t == "-cum" {
				vcopy.set("cum", "t")
				continue
			}
			ignore = catRegex(ignore, t[1:])
		default:
			focus = catRegex(focus, t)
		}
	}

	if name == "tags" {
		updateFocusIgnore(vcopy, "tag", focus, ignore)
	} else {
		updateFocusIgnore(vcopy, "", focus, ignore)
	}

	if vcopy["nodecount"].intValue() == -1 && (name == "text" || name == "top") {
		vcopy.set("nodecount", "10")
	}

	return cmd, vcopy, nil
}

func updateFocusIgnore(v variables, prefix, f, i string) {
	if f != "" {
		focus := prefix + "focus"
		v.set(focus, catRegex(v[focus].value, f))
	}

	if i != "" {
		ignore := prefix + "ignore"
		v.set(ignore, catRegex(v[ignore].value, i))
	}
}

func catRegex(a, b string) string {
	if a != "" && b != "" {
		return a + "|" + b
	}
	return a + b
}

// commandHelp displays help and usage information for all Commands
// and Variables or a specific Command or Variable.
func commandHelp(args string, ui plugin.UI) {
	if args == "" {
		help := usage(false)
		help = help + `
  :   Clear focus/ignore/hide/tagfocus/tagignore

  type "help <cmd|option>" for more information
`

		ui.Print(help)
		return
	}

	if c := pprofCommands[args]; c != nil {
		ui.Print(c.help(args))
		return
	}

	if v := pprofVariables[args]; v != nil {
		ui.Print(v.help + "\n")
		return
	}

	ui.PrintErr("Unknown command: " + args)
}

// newCompleter creates an autocompletion function for a set of commands.
func newCompleter(fns []string) func(string) string {
	return func(line string) string {
		v := pprofVariables
		switch tokens := strings.Fields(line); len(tokens) {
		case 0:
			// Nothing to complete
		case 1:
			// Single token -- complete command name
			if match := matchVariableOrCommand(v, tokens[0]); match != "" {
				return match
			}
		case 2:
			if tokens[0] == "help" {
				if match := matchVariableOrCommand(v, tokens[1]); match != "" {
					return tokens[0] + " " + match
				}
				return line
			}
			fallthrough
		default:
			// Multiple tokens -- complete using functions, except for tags
			if cmd := pprofCommands[tokens[0]]; cmd != nil && tokens[0] != "tags" {
				lastTokenIdx := len(tokens) - 1
				lastToken := tokens[lastTokenIdx]
				if strings.HasPrefix(lastToken, "-") {
					lastToken = "-" + functionCompleter(lastToken[1:], fns)
				} else {
					lastToken = functionCompleter(lastToken, fns)
				}
				return strings.Join(append(tokens[:lastTokenIdx], lastToken), " ")
			}
		}
		return line
	}
}

// matchCommand attempts to match a string token to the prefix of a Command.
func matchVariableOrCommand(v variables, token string) string {
	token = strings.ToLower(token)
	found := ""
	for cmd := range pprofCommands {
		if strings.HasPrefix(cmd, token) {
			if found != "" {
				return ""
			}
			found = cmd
		}
	}
	for variable := range v {
		if strings.HasPrefix(variable, token) {
			if found != "" {
				return ""
			}
			found = variable
		}
	}
	return found
}

// functionCompleter replaces provided substring with a function
// name retrieved from a profile if a single match exists. Otherwise,
// it returns unchanged substring. It defaults to no-op if the profile
// is not specified.
func functionCompleter(substring string, fns []string) string {
	found := ""
	for _, fName := range fns {
		if strings.Contains(fName, substring) {
			if found != "" {
				return substring
			}
			found = fName
		}
	}
	if found != "" {
		return found
	}
	return substring
}

func functionNames(p *profile.Profile) []string {
	var fns []string
	for _, fn := range p.Function {
		fns = append(fns, fn.Name)
	}
	return fns
}
