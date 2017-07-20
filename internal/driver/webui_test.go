// Copyright 2017 Google Inc. All Rights Reserved.
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
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os/exec"
	"regexp"
	"testing"

	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

func TestWebInterface(t *testing.T) {
	prof := makeFakeProfile()
	ui := &webInterface{
		prof:    prof,
		options: &plugin.Options{Obj: fakeObjTool{}},
		help:    make(map[string]string),
	}

	// Start test server.
	server := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			switch r.URL.Path {
			case "/":
				ui.dot(w, r)
			case "/disasm":
				ui.disasm(w, r)
			case "/peek":
				ui.peek(w, r)
			case "/weblist":
				ui.weblist(w, r)
			}
		}))
	defer server.Close()

	haveDot := false
	if _, err := exec.LookPath("dot"); err == nil {
		haveDot = true
	}

	type testCase struct {
		path    string
		want    []string
		needDot bool
	}
	testcases := []testCase{
		{"/", []string{"F1", "F2", "F3", "testbin", "cpu"}, true},
		{"/weblist?f=" + url.QueryEscape("F[12]"),
			[]string{"F1", "F2", "300ms line1"}, false},
		{"/peek?f=" + url.QueryEscape("F[12]"),
			[]string{"300ms.*F1", "200ms.*300ms.*F2"}, false},
		{"/disasm?f=" + url.QueryEscape("F[12]"),
			[]string{"f1:asm", "f2:asm"}, false},
	}
	for _, c := range testcases {
		if c.needDot && !haveDot {
			t.Log("skpping", c.path, "since dot (graphviz) does not seem to be installed")
			continue
		}

		res, err := http.Get(server.URL + c.path)
		if err != nil {
			t.Error("could not fetch", c.path, err)
			continue
		}
		data, err := ioutil.ReadAll(res.Body)
		if err != nil {
			t.Error("could not read response", c.path, err)
			continue
		}
		result := string(data)
		for _, w := range c.want {
			if match, _ := regexp.MatchString(w, result); !match {
				t.Errorf("response for %s does not match "+
					"expected pattern '%s'; "+
					"actual result:\n%s", c.path, w, result)
			}
		}
	}

}

// Implement fake object file support.

const addrBase = 0x1000
const fakeSource = "testdata/file1000.src"

type fakeObj struct{}

func (f fakeObj) Close() error    { return nil }
func (f fakeObj) Name() string    { return "testbin" }
func (f fakeObj) Base() uint64    { return 0 }
func (f fakeObj) BuildID() string { return "" }
func (f fakeObj) SourceLine(addr uint64) ([]plugin.Frame, error) {
	return nil, fmt.Errorf("SourceLine unimplemented")
}
func (f fakeObj) Symbols(r *regexp.Regexp, addr uint64) ([]*plugin.Sym, error) {
	return []*plugin.Sym{
		{[]string{"F1"}, fakeSource, addrBase, addrBase + 10},
		{[]string{"F2"}, fakeSource, addrBase + 10, addrBase + 20},
		{[]string{"F3"}, fakeSource, addrBase + 20, addrBase + 30},
	}, nil
}

type fakeObjTool struct{}

func (obj fakeObjTool) Open(file string, start, limit, offset uint64) (plugin.ObjFile, error) {
	return fakeObj{}, nil
}

func (obj fakeObjTool) Disasm(file string, start, end uint64) ([]plugin.Inst, error) {
	return []plugin.Inst{
		{Addr: addrBase + 0, Text: "f1:asm", Function: "F1"},
		{Addr: addrBase + 10, Text: "f2:asm", Function: "F2"},
		{Addr: addrBase + 20, Text: "d3:asm", Function: "F3"},
	}, nil
}

func makeFakeProfile() *profile.Profile {
	// Three functions: F1, F2, F3 with three lines, 11, 22, 33.
	funcs := []*profile.Function{
		{ID: 1, Name: "F1", Filename: fakeSource, StartLine: 3},
		{ID: 2, Name: "F2", Filename: fakeSource, StartLine: 5},
		{ID: 3, Name: "F3", Filename: fakeSource, StartLine: 7},
	}
	lines := []profile.Line{
		{Function: funcs[0], Line: 11},
		{Function: funcs[1], Line: 22},
		{Function: funcs[2], Line: 33},
	}
	mapping := []*profile.Mapping{
		{
			ID:             1,
			Start:          addrBase,
			Limit:          addrBase + 10,
			Offset:         0,
			File:           "testbin",
			HasFunctions:   true,
			HasFilenames:   true,
			HasLineNumbers: true,
		},
	}

	// Three interesting addresses: base+{10,20,30}
	locs := []*profile.Location{
		{ID: 1, Address: addrBase + 10, Line: lines[0:1], Mapping: mapping[0]},
		{ID: 2, Address: addrBase + 20, Line: lines[1:2], Mapping: mapping[0]},
		{ID: 3, Address: addrBase + 30, Line: lines[2:3], Mapping: mapping[0]},
	}

	// Two stack traces.
	return &profile.Profile{
		PeriodType:    &profile.ValueType{Type: "cpu", Unit: "milliseconds"},
		Period:        1,
		DurationNanos: 10e9,
		SampleType: []*profile.ValueType{
			{Type: "cpu", Unit: "milliseconds"},
		},
		Sample: []*profile.Sample{
			{
				Location: []*profile.Location{locs[2], locs[1], locs[0]},
				Value:    []int64{100},
			},
			{
				Location: []*profile.Location{locs[1], locs[0]},
				Value:    []int64{200},
			},
		},
		Location: locs,
		Function: funcs,
		Mapping:  mapping,
	}
}

func TestNewListenerAndURL(t *testing.T) {
	tests := []struct {
		hostport  string
		wantErr   bool
		wantURLRe *regexp.Regexp
		wantLocal bool
	}{
		{
			hostport:  ":",
			wantURLRe: regexp.MustCompile(`http://localhost:\d+`),
			wantLocal: true,
		},
		{
			hostport:  "localhost:",
			wantURLRe: regexp.MustCompile(`http://localhost:\d+`),
			wantLocal: true,
		},
		{
			hostport:  "127.0.0.1:",
			wantURLRe: regexp.MustCompile(`http://127\.0\.0\.1:\d+`),
			wantLocal: true,
		},
		{
			hostport:  "localhost:12344",
			wantURLRe: regexp.MustCompile(`http://localhost:12344`),
			wantLocal: true,
		},
		{
			hostport: "http://localhost:12345",
			wantErr:  true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.hostport, func(t *testing.T) {
			_, url, isLocal, err := newListenerAndURL(tt.hostport)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("newListenerAndURL(%v) want error; got none", tt.hostport)
				}
				return
			}
			if err != nil {
				t.Fatalf("newListenerAndURL(%v) = %v", tt.hostport, err)
			}
			if !tt.wantURLRe.MatchString(url) {
				t.Errorf("newListenerAndURL(%v) URL = %v; want URL matching %v", tt.hostport, url, tt.wantURLRe)
			}
			if got, want := isLocal, tt.wantLocal; got != want {
				t.Errorf("newListenerAndURL(%v) isLocal = %v; want %v", tt.hostport, got, want)
			}
		})
	}
}
