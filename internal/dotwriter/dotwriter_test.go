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

package dotwriter

import (
	"bytes"
	"strings"
	"testing"
)

func TestEscaped(t *testing.T) {
	for _, tc := range []struct {
		desc  string
		input string
		want  string
	}{
		{
			desc:  "multiple double quotes",
			input: `label: "foo" and "bar"`,
			want:  `label: \"foo\" and \"bar\"`,
		},
		{
			desc:  "newline becomes left-justified line break",
			input: "label: foo \n bar",
			want:  `label: foo \l bar`,
		},
		{
			desc:  "two backslashes",
			input: `label: \\`,
			want:  `label: \\\\`,
		},
		{
			desc:  "adjacent double quotes",
			input: `label: ""`,
			want:  `label: \"\"`,
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			if got := Escaped(tc.input).String(); got != tc.want {
				t.Errorf("Escaped(%q) = %q, want %q", tc.input, got, tc.want)
			}
		})
	}
}

func TestWriterEscapesUntrustedInputs(t *testing.T) {
	var buf bytes.Buffer
	w := Begin(&buf, `bad"graph`)
	w.Legend(`bad"legend`, Attr{"label", LinesLeft([]string{`l1"q`, "l2\nl3"})})
	w.Node(ID(`bad"id`),
		Attr{"label", `val" URL="javascript:alert(1)" x="`},
		Attr{"shape", ID(`bad"shape`)},
		Attr{"fontsize", 12},
	)
	w.Edge(ID("N1"), ID(`2leading`), Attr{"tooltip", `tip"quote`})
	w.End()

	got := buf.String()
	for _, forbidden := range []string{
		`bad"graph`,
		`bad"legend`,
		`l1"q`,
		`bad"id`,
		`URL="javascript:alert(1)"`,
		`bad"shape`,
		` 2leading `,
		`tip"quote`,
	} {
		if strings.Contains(got, forbidden) {
			t.Errorf("DOT output contains unescaped %q:\n%s", forbidden, got)
		}
	}
	for _, want := range []string{
		`digraph "bad\"graph"`,
		`subgraph cluster_L { "bad\"legend"`,
		`label="l1\"q\ll2\ll3\l"`,
		`"bad\"id"`,
		`URL=\"javascript:alert(1)\"`,
		`shape="bad\"shape"`,
		`N1 -> "2leading"`,
		`tooltip="tip\"quote"`,
	} {
		if !strings.Contains(got, want) {
			t.Errorf("DOT output missing %q:\n%s", want, got)
		}
	}
}
