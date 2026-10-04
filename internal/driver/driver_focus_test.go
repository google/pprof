// Copyright 2026 Google Inc. All Rights Reserved.
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
	"errors"
	"fmt"
	"strings"
	"testing"

	"github.com/google/pprof/internal/proftest"
	"github.com/google/pprof/profile"
)

type tagFilterDiagnosticUI struct {
	proftest.TestUI
	messages []string
}

func (ui *tagFilterDiagnosticUI) PrintErr(args ...interface{}) {
	ui.messages = append(ui.messages, fmt.Sprint(args...))
}

func TestCompileTagFilterRangeDiagnostics(t *testing.T) {
	tests := []struct {
		name, value string
		labels      map[string][]int64
		units       map[string][]string
		want        bool
		fallback    bool
		warning     []string
	}{
		{
			name: "unitless lower bound", value: "request=0:256B",
			labels: map[string][]int64{"request": {128}}, fallback: true,
			warning: []string{"range bounds have incompatible units", `"count"`, `"B"`, `"request=0:256B"`, "interpreting as regexp"},
		},
		{
			name: "different bound dimensions", value: "request=1B:2s",
			labels: map[string][]int64{"request": {1}}, fallback: true,
			warning: []string{"range bounds have incompatible units", `"B"`, `"s"`, `"request=1B:2s"`, "interpreting as regexp"},
		},
		{
			name: "unkeyed mixed bounds", value: "0:256B",
			labels: map[string][]int64{"request": {128}}, fallback: true,
			warning: []string{"range bounds have incompatible units", `"count"`, `"B"`, `"0:256B"`, "interpreting as regexp"},
		},
		{
			name: "inferred bytes", value: "request=0:256",
			labels:  map[string][]int64{"request": {128}},
			warning: []string{`range "0:256" has unit "count"`, `incompatible with numeric tag units: "request" ("bytes")`},
		},
		{
			name: "selected incompatible key despite compatible key", value: "request=0:256",
			labels:  map[string][]int64{"request": {128}, "pid": {128}},
			warning: []string{`range "0:256" has unit "count"`, `incompatible with numeric tag units: "request" ("bytes")`},
		},
		{
			name: "all unkeyed units incompatible and sorted", value: "0:256",
			labels: map[string][]int64{"request": {128}, "elapsed": {1}},
			units:  map[string][]string{"elapsed": {"seconds"}},
			warning: []string{`range "0:256" has unit "count"`,
				`incompatible with numeric tag units: "elapsed" ("seconds"), "request" ("bytes")`},
		},
		{
			name: "inferred count key", value: "pid=0:256",
			labels: map[string][]int64{"pid": {128}}, want: true,
		},
		{
			name: "unkeyed compatible unit suppresses warning", value: "0:256",
			labels: map[string][]int64{"request": {128}, "pid": {128}}, want: true,
		},
		{
			name: "unkeyed compatible unit outside range suppresses warning", value: "0:256",
			labels: map[string][]int64{"request": {128}, "pid": {257}},
		},
		{
			name: "inferred count outside range", value: "pid=0:256",
			labels: map[string][]int64{"pid": {257}},
		},
		{
			name: "selected compatible key ignores other units", value: "pid=0:256",
			labels: map[string][]int64{"request": {128}, "pid": {128}}, want: true,
		},
		{
			name: "absent selected key", value: "missing=0:256",
			labels: map[string][]int64{"request": {128}},
		},
		{name: "empty unit map", value: "0:256"},
		{
			name: "legacy unitless upper bound", value: "request=0B:256",
			labels: map[string][]int64{"request": {256}}, want: true,
		},
		{
			name: "unknown units retain permissive matching", value: "custom=1foo:2bar",
			labels: map[string][]int64{"custom": {2}}, units: map[string][]string{"custom": {"bar"}}, want: true,
		},
		{
			name: "converted inclusive lower bound", value: "request=1kB:2kB",
			labels: map[string][]int64{"request": {1024}}, want: true,
		},
		{
			name: "converted inclusive upper bound", value: "request=1kB:2kB",
			labels: map[string][]int64{"request": {2048}}, want: true,
		},
		{
			name: "converted outside bound", value: "request=1kB:2kB",
			labels: map[string][]int64{"request": {2049}},
		},
		{
			name: "open upper bound", value: "request=1kB:",
			labels: map[string][]int64{"request": {1024}}, want: true,
		},
		{
			name: "open lower bound", value: "request=:1kB",
			labels: map[string][]int64{"request": {1024}}, want: true,
		},
		{
			name: "negative bound", value: "request=-2kB:-1kB",
			labels: map[string][]int64{"request": {-1024}}, want: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			sample := &profile.Sample{NumLabel: tt.labels, NumUnit: tt.units}
			p := &profile.Profile{Sample: []*profile.Sample{sample}}
			units, _ := p.NumLabelUnits()
			ui := &tagFilterDiagnosticUI{TestUI: proftest.TestUI{T: t}}
			filter, err := compileTagFilter("tagfocus", tt.value, units, ui, nil)
			if err != nil {
				t.Fatal(err)
			}
			if got := filter(sample); got != tt.want {
				t.Errorf("numeric match = %v, want %v", got, tt.want)
			}
			if tt.fallback {
				_, text, keyed := strings.Cut(tt.value, "=")
				if !keyed {
					text = tt.value
				}
				sample.Label = map[string][]string{"request": {text}}
				if !filter(sample) {
					t.Error("regexp fallback does not match the literal string label")
				}
			}

			messages := ui.messages
			if !tt.fallback {
				_, value, keyed := strings.Cut(tt.value, "=")
				if !keyed {
					value = tt.value
				}
				info := "tagfocus:Interpreted '" + value + "' as range, not regexp"
				if len(messages) == 0 || messages[0] != info {
					t.Fatalf("range interpretation message missing: %q", messages)
				}
				messages = messages[1:]
			}
			wantWarnings := 0
			if len(tt.warning) > 0 {
				wantWarnings = 1
			}
			if len(messages) != wantWarnings {
				t.Fatalf("warnings = %q, want %d warning(s)", messages, wantWarnings)
			}
			if wantWarnings > 0 && !strings.HasPrefix(messages[0], "tagfocus:") {
				t.Errorf("warning does not identify the filter option: %q", messages[0])
			}
			for _, text := range tt.warning {
				if !strings.Contains(messages[0], text) {
					t.Errorf("warning %q does not contain %q", messages[0], text)
				}
			}
		})
	}
}

func TestCompileTagFilterRegexpDiagnostics(t *testing.T) {
	for _, value := range []string{"request=a.*,b.*", "request=a,b", "request=0:256B,other", "request=^0:256B$"} {
		t.Run(value, func(t *testing.T) {
			ui := &proftest.TestUI{T: t}
			filter, err := compileTagFilter("tagignore", value, nil, ui, nil)
			if err != nil {
				t.Fatal(err)
			}
			sample := &profile.Sample{Label: map[string][]string{"request": {"abc", "0:256B"}}}
			if !filter(sample) {
				t.Error("regexp or comma matching changed")
			}
		})
	}
	t.Run("invalid regexp", func(t *testing.T) {
		filter, err := compileTagFilter("tagignore", "request=[", nil, &proftest.TestUI{T: t}, nil)
		if filter != nil || err == nil || !strings.Contains(err.Error(), "parsing tagignore regexp") {
			t.Errorf("got nil filter %v, error %v; want regexp error", filter == nil, err)
		}
	})
	t.Run("existing error", func(t *testing.T) {
		want := errors.New("previous error")
		filter, err := compileTagFilter("tagignore", "request=0:256B", nil, &proftest.TestUI{T: t}, want)
		if filter != nil || err != want {
			t.Errorf("got nil filter %v, error %v; want original error", filter == nil, err)
		}
	})
}
