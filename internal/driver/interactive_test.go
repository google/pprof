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
	"math/rand"
	"strings"
	"testing"

	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/proftest"
	"github.com/google/pprof/internal/report"
	"github.com/google/pprof/internal/transport"
	"github.com/google/pprof/profile"
)

func TestShell(t *testing.T) {
	p := &profile.Profile{}
	generateReportWrapper = checkValue
	defer func() { generateReportWrapper = generateReport }()

	// Use test commands and variables to exercise interactive processing
	var savedCommands commands
	savedCommands, pprofCommands = pprofCommands, testCommands
	defer func() { pprofCommands = savedCommands }()

	savedConfig := currentConfig()
	defer setCurrentConfig(savedConfig)

	shortcuts1, scScript1 := makeShortcuts(interleave(script, 2), 1)
	shortcuts2, scScript2 := makeShortcuts(interleave(script, 1), 2)

	var testcases = []struct {
		name              string
		input             []string
		shortcuts         shortcuts
		allowRx           string
		numAllowRxMatches int
		propagateError    bool
	}{
		{"Random interleave of independent scripts 1", interleave(script, 0), pprofShortcuts, "", 0, false},
		{"Random interleave of independent scripts 2", interleave(script, 1), pprofShortcuts, "", 0, false},
		{"Random interleave of independent scripts with shortcuts 1", scScript1, shortcuts1, "", 0, false},
		{"Random interleave of independent scripts with shortcuts 2", scScript2, shortcuts2, "", 0, false},
		{"Group with invalid value", []string{"sort=this"}, pprofShortcuts, `invalid "sort" value`, 1, false},
		{"No special value provided for the option", []string{"sample_index"}, pprofShortcuts, `please specify a value, e.g. sample_index=<val>`, 1, false},
		{"No string value provided for the option", []string{"focus"}, pprofShortcuts, `please specify a value, e.g. focus=<val>`, 1, false},
		{"No float value provided for the option", []string{"divide_by"}, pprofShortcuts, `please specify a value, e.g. divide_by=<val>`, 1, false},
		{"Helpful input format reminder", []string{"sample_index 0"}, pprofShortcuts, `did you mean: sample_index=0`, 1, false},
		{"Verify propagation of IO errors", []string{"**error**"}, pprofShortcuts, "", 0, true},
	}

	o := setDefaults(&plugin.Options{HTTPTransport: transport.New(nil)})
	for _, tc := range testcases {
		t.Run(tc.name, func(t *testing.T) {
			setCurrentConfig(savedConfig)
			pprofShortcuts = tc.shortcuts
			ui := &proftest.TestUI{
				T:       t,
				Input:   tc.input,
				AllowRx: tc.allowRx,
			}
			o.UI = ui

			err := interactive(p, o)
			if (tc.propagateError && err == nil) || (!tc.propagateError && err != nil) {
				t.Errorf("%s: %v", tc.name, err)
			}

			// Confirm error message written out once.
			if tc.numAllowRxMatches != ui.NumAllowRxMatches {
				t.Errorf("want error message to be printed %d time(s), got %d",
					tc.numAllowRxMatches, ui.NumAllowRxMatches)
			}
		})
	}
}

var testCommands = commands{
	"check": &command{report.Raw, nil, nil, true, "", ""},
}

// script contains sequences of commands to be executed for testing. Commands
// are split by semicolon and interleaved randomly, so they must be
// independent from each other.
var script = []string{
	"call_tree=true;call_tree=false;check call_tree=false;call_tree=yes;check call_tree=true",
	"mean=1;check mean=true;mean=n;check mean=false",
	"nodecount=-1;nodecount=-2;check nodecount=-2;nodecount=999999;check nodecount=999999",
	"nodefraction=-1;nodefraction=-2.5;check nodefraction=-2.5;nodefraction=0.0001;check nodefraction=0.0001",
	"focus=one;focus=two;check focus=two",
	"flat=true;check sort=flat;cum=1;check sort=cum",
}

func makeShortcuts(input []string, seed int) (shortcuts, []string) {
	rand.Seed(int64(seed))

	s := shortcuts{}
	var output, chunk []string
	for _, l := range input {
		chunk = append(chunk, l)
		switch rand.Intn(3) {
		case 0:
			// Create a macro for commands in 'chunk'.
			macro := fmt.Sprintf("alias%d", len(s))
			s[macro] = chunk
			output = append(output, macro)
			chunk = nil
		case 1:
			// Append commands in 'chunk' by themselves.
			output = append(output, chunk...)
			chunk = nil
		case 2:
			// Accumulate commands into 'chunk'
		}
	}
	output = append(output, chunk...)
	return s, output
}

func checkValue(p *profile.Profile, cmd []string, cfg config, o *plugin.Options) error {
	if len(cmd) != 2 {
		return fmt.Errorf("expected len(cmd)==2, got %v", cmd)
	}

	input := cmd[1]
	args := strings.SplitN(input, "=", 2)
	if len(args) == 0 {
		return fmt.Errorf("unexpected empty input")
	}
	name, value := args[0], ""
	if len(args) == 2 {
		value = args[1]
	}

	f, ok := configFieldMap[name]
	if !ok {
		return fmt.Errorf("Could not find variable named %s", name)
	}

	if got := cfg.get(f); got != value {
		return fmt.Errorf("Variable %s, want %s, got %s", name, value, got)
	}
	return nil
}

func interleave(input []string, seed int) []string {
	var inputs [][]string
	for _, s := range input {
		inputs = append(inputs, strings.Split(s, ";"))
	}
	rand.Seed(int64(seed))
	var output []string
	for len(inputs) > 0 {
		next := rand.Intn(len(inputs))
		output = append(output, inputs[next][0])
		if tail := inputs[next][1:]; len(tail) > 0 {
			inputs[next] = tail
		} else {
			inputs = append(inputs[:next], inputs[next+1:]...)
		}
	}
	return output
}

func TestInteractiveCommands(t *testing.T) {
	type interactiveTestcase struct {
		input string
		want  map[string]string
	}

	testcases := []interactiveTestcase{
		{
			"top 10 --cum focus1 -ignore focus2",
			map[string]string{
				"granularity": "functions",
				"nodecount":   "10",
				"sort":        "cum",
				"focus":       "focus1|focus2",
				"ignore":      "ignore",
			},
		},
		{
			"top10 --cum focus1 -ignore focus2",
			map[string]string{
				"granularity": "functions",
				"nodecount":   "10",
				"sort":        "cum",
				"focus":       "focus1|focus2",
				"ignore":      "ignore",
			},
		},
		{
			"dot",
			map[string]string{
				"granularity": "functions",
				"nodecount":   "80",
				"sort":        "flat",
			},
		},
		{
			"tags   -ignore1 -ignore2 focus1 >out",
			map[string]string{
				"granularity": "functions",
				"nodecount":   "80",
				"sort":        "flat",
				"output":      "out",
				"tagfocus":    "focus1",
				"tagignore":   "ignore1|ignore2",
			},
		},
		{
			"weblist  find -test",
			map[string]string{
				"granularity": "addresses",
				"noinlines":   "false",
				"nodecount":   "0",
				"sort":        "flat",
				"ignore":      "test",
			},
		},
		{
			"callgrind   fun -ignore  >out",
			map[string]string{
				"granularity": "addresses",
				"nodecount":   "0",
				"sort":        "flat",
				"output":      "out",
			},
		},
		{
			"999",
			nil, // Error
		},
	}

	for _, tc := range testcases {
		cmd, cfg, err := parseCommandLine(strings.Fields(tc.input))
		if tc.want == nil && err != nil {
			// Error expected
			continue
		}
		if err != nil {
			t.Errorf("failed on %q: %v", tc.input, err)
			continue
		}

		// Get report output format
		c := pprofCommands[cmd[0]]
		if c == nil {
			t.Fatalf("unexpected nil command")
		}
		cfg = applyCommandOverrides(cmd[0], c.format, cfg)

		for n, want := range tc.want {
			if got := cfg.get(configFieldMap[n]); got != want {
				t.Errorf("failed on %q, cmd=%q, %s got %s, want %s", tc.input, cmd, n, got, want)
			}
		}
	}
}
