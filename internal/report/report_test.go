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

package report

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/google/pprof/internal/binutils"
	"github.com/google/pprof/internal/graph"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/proftest"
	"github.com/google/pprof/profile"
)

type testcase struct {
	rpt  *Report
	want string
}

func TestSource(t *testing.T) {
	const path = "testdata/"

	sampleValue1 := func(v []int64) int64 {
		return v[1]
	}

	for _, tc := range []testcase{
		{
			rpt: New(
				testProfile.Copy(),
				&Options{
					OutputFormat: List,
					Symbol:       regexp.MustCompile(`.`),
					TrimPath:     "/some/path",

					SampleValue: sampleValue1,
					SampleUnit:  testProfile.SampleType[1].Unit,
				},
			),
			want: path + "source.rpt",
		},
		{
			rpt: New(
				testProfile.Copy(),
				&Options{
					OutputFormat: Dot,
					CallTree:     true,
					Symbol:       regexp.MustCompile(`.`),
					TrimPath:     "/some/path",

					SampleValue: sampleValue1,
					SampleUnit:  testProfile.SampleType[1].Unit,
				},
			),
			want: path + "source.dot",
		},
	} {
		var b bytes.Buffer
		if err := Generate(&b, tc.rpt, &binutils.Binutils{}); err != nil {
			t.Fatalf("%s: %v", tc.want, err)
		}

		gold, err := os.ReadFile(tc.want)
		if err != nil {
			t.Fatalf("%s: %v", tc.want, err)
		}
		if runtime.GOOS == "windows" {
			if tc.rpt.options.OutputFormat == Dot {
				// The .dot test has the paths inside strings, so \ must be escaped.
				gold = bytes.ReplaceAll(gold, []byte("testdata/"), []byte(`testdata\\`))
			} else {
				gold = bytes.ReplaceAll(gold, []byte("testdata/"), []byte(`testdata\`))
			}
		}
		if string(b.String()) != string(gold) {
			d, err := proftest.Diff(gold, b.Bytes())
			if err != nil {
				t.Fatalf("%s: %v", "source", err)
			}
			t.Error("source" + "\n" + string(d) + "\n" + "gold:\n" + tc.want)
		}
	}
}

// TestFilter ensures that commands with a regexp filter argument return an
// error if there are no results.
func TestFilter(t *testing.T) {
	const filter = "doesNotExist"

	tests := []struct {
		name   string
		format int
	}{
		{
			name:   "list",
			format: List,
		},
		{
			name:   "disasm",
			format: Dis,
		},
		{
			// N.B. Tree with a Symbol is "peek".
			name:   "peek",
			format: Tree,
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			rpt := New(testProfile.Copy(), &Options{
				OutputFormat: tc.format,
				Symbol:       regexp.MustCompile(filter),
				SampleValue:  func(v []int64) int64 { return v[1] },
				SampleUnit:   testProfile.SampleType[1].Unit,
			})

			var buf bytes.Buffer
			err := Generate(&buf, rpt, &binutils.Binutils{})
			if err == nil {
				t.Fatalf("Generate got nil, want error; buf = %s", buf.String())
			}
			if !strings.Contains(err.Error(), filter) {
				t.Errorf("Error got %v, want it to contain %q", err, filter)
			}
		})
	}
}

// testM contains mappings for fake profiles used in tests.
var testM = []*profile.Mapping{
	{
		ID:              1,
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
}

// testF contains functions for fake profiles used in tests.
var testF = []*profile.Function{
	{
		ID:       1,
		Name:     "main",
		Filename: "testdata/source1",
	},
	{
		ID:       2,
		Name:     "foo",
		Filename: "testdata/source1",
	},
	{
		ID:       3,
		Name:     "bar",
		Filename: "testdata/source1",
	},
	{
		ID:       4,
		Name:     "tee",
		Filename: "/some/path/testdata/source2",
	},
}

// testL contains locations for fake profiles used in tests.
var testL = []*profile.Location{
	{
		ID:      1,
		Mapping: testM[0],
		Line: []profile.Line{
			{
				Function: testF[0],
				Line:     2,
				Column:   2,
			},
		},
	},
	{
		ID:      2,
		Mapping: testM[0],
		Line: []profile.Line{
			{
				Function: testF[1],
				Line:     4,
				Column:   4,
			},
		},
	},
	{
		ID:      3,
		Mapping: testM[0],
		Line: []profile.Line{
			{
				Function: testF[2],
				Line:     10,
			},
		},
	},
	{
		ID:      4,
		Mapping: testM[0],
		Line: []profile.Line{
			{
				Function: testF[3],
				Line:     2,
			},
		},
	},
	{
		ID:      5,
		Mapping: testM[0],
		Line: []profile.Line{
			{
				Function: testF[3],
				Line:     8,
			},
		},
	},
	{
		ID:      6,
		Mapping: testM[0],
		Line: []profile.Line{
			{
				Function: testF[3],
				Line:     7,
			},
			{
				Function: testF[2],
				Line:     6,
			},
		},
	},
}

// testSample returns a profile sample with specified value and stack.
// Note: callees come first in sample stacks.
func testSample(value int64, locs ...*profile.Location) *profile.Sample {
	return &profile.Sample{
		Value:    []int64{value},
		Location: locs,
	}
}

// makeTestProfile returns a profile with specified samples that uses testL/testF/testM
// (defined in report_test.go).
func makeTestProfile(samples ...*profile.Sample) *profile.Profile {
	return &profile.Profile{
		SampleType: []*profile.ValueType{{Type: "samples", Unit: "count"}},
		Sample:     samples,
		Location:   testL,
		Function:   testF,
		Mapping:    testM,
	}
}

// testProfile contains a fake profile used in tests.
// Various report methods modify profiles so tests should operate on testProfile.Copy().
var testProfile = &profile.Profile{
	PeriodType:    &profile.ValueType{Type: "cpu", Unit: "millisecond"},
	Period:        10,
	DurationNanos: 10e9,
	SampleType: []*profile.ValueType{
		{Type: "samples", Unit: "count"},
		{Type: "cpu", Unit: "cycles"},
	},
	Sample: []*profile.Sample{
		{
			Location: []*profile.Location{testL[0]},
			Value:    []int64{1, 1},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{1, 10},
		},
		{
			Location: []*profile.Location{testL[4], testL[2], testL[0]},
			Value:    []int64{1, 100},
		},
		{
			Location: []*profile.Location{testL[3], testL[0]},
			Value:    []int64{1, 1000},
		},
		{
			Location: []*profile.Location{testL[4], testL[3], testL[0]},
			Value:    []int64{1, 10000},
		},
	},
	Location: testL,
	Function: testF,
	Mapping:  testM,
}

func TestDisambiguation(t *testing.T) {
	parent1 := &graph.Node{Info: graph.NodeInfo{Name: "parent1"}}
	parent2 := &graph.Node{Info: graph.NodeInfo{Name: "parent2"}}
	child1 := &graph.Node{Info: graph.NodeInfo{Name: "child"}, Function: parent1}
	child2 := &graph.Node{Info: graph.NodeInfo{Name: "child"}, Function: parent2}
	child3 := &graph.Node{Info: graph.NodeInfo{Name: "child"}, Function: parent1}
	sibling := &graph.Node{Info: graph.NodeInfo{Name: "sibling"}, Function: parent1}

	n := []*graph.Node{parent1, parent2, child1, child2, child3, sibling}

	wanted := map[*graph.Node]string{
		parent1: "parent1",
		parent2: "parent2",
		child1:  "child [1/2]",
		child2:  "child [2/2]",
		child3:  "child [1/2]",
		sibling: "sibling",
	}

	g := &graph.Graph{Nodes: n}

	names := getDisambiguatedNames(g)

	for node, want := range wanted {
		if got := names[node]; got != want {
			t.Errorf("name %s, got %s, want %s", node.Info.Name, got, want)
		}
	}
}

func TestFunctionMap(t *testing.T) {

	fm := make(functionMap)
	nodes := []graph.NodeInfo{
		{Name: "fun1"},
		{Name: "fun2", File: "filename"},
		{Name: "fun1"},
		{Name: "fun2", File: "filename2"},
	}

	want := []struct {
		wantFunction profile.Function
		wantAdded    bool
	}{
		{profile.Function{ID: 1, Name: "fun1"}, true},
		{profile.Function{ID: 2, Name: "fun2", Filename: "filename"}, true},
		{profile.Function{ID: 1, Name: "fun1"}, false},
		{profile.Function{ID: 3, Name: "fun2", Filename: "filename2"}, true},
	}

	for i, tc := range nodes {
		gotFunc, gotAdded := fm.findOrAdd(tc)
		if got, want := gotFunc, want[i].wantFunction; *got != want {
			t.Errorf("%d: got %v, want %v", i, got, want)
		}
		if got, want := gotAdded, want[i].wantAdded; got != want {
			t.Errorf("%d: got %v, want %v", i, got, want)
		}
	}
}

func TestLegendActiveFilters(t *testing.T) {
	activeFilterInput := []string{
		"focus=123|456|789|101112|131415|161718|192021|222324|252627|282930|313233|343536|363738|acbdefghijklmnop",
		"show=short filter",
	}
	expectedLegendActiveFilter := []string{
		"Active filters:",
		"   focus=123|456|789|101112|131415|161718|192021|222324|252627|282930|313233|343536…",
		"   show=short filter",
	}
	legendActiveFilter := legendActiveFilters(activeFilterInput)
	if len(legendActiveFilter) != len(expectedLegendActiveFilter) {
		t.Errorf("wanted length %v got length %v", len(expectedLegendActiveFilter), len(legendActiveFilter))
	}
	for i := range legendActiveFilter {
		if legendActiveFilter[i] != expectedLegendActiveFilter[i] {
			t.Errorf("%d: want \"%v\", got \"%v\"", i, expectedLegendActiveFilter[i], legendActiveFilter[i])
		}
	}
}

func TestComputeTotal(t *testing.T) {
	p1 := testProfile.Copy()
	p1.Sample = []*profile.Sample{
		{
			Location: []*profile.Location{testL[0]},
			Value:    []int64{1, 1},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{1, 10},
		},
		{
			Location: []*profile.Location{testL[4], testL[2], testL[0]},
			Value:    []int64{1, 100},
		},
	}

	p2 := testProfile.Copy()
	p2.Sample = []*profile.Sample{
		{
			Location: []*profile.Location{testL[0]},
			Value:    []int64{1, 1},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{1, -10},
		},
		{
			Location: []*profile.Location{testL[4], testL[2], testL[0]},
			Value:    []int64{1, 100},
		},
	}

	p3 := testProfile.Copy()
	p3.Sample = []*profile.Sample{
		{
			Location: []*profile.Location{testL[0]},
			Value:    []int64{10000, 1},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{-10, 3},
			Label:    map[string][]string{"pprof::base": {"true"}},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{1000, -10},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{-9000, 3},
			Label:    map[string][]string{"pprof::base": {"true"}},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{-1, 3},
			Label:    map[string][]string{"pprof::base": {"true"}},
		},
		{
			Location: []*profile.Location{testL[4], testL[2], testL[0]},
			Value:    []int64{100, 100},
		},
		{
			Location: []*profile.Location{testL[2], testL[1], testL[0]},
			Value:    []int64{100, 3},
			Label:    map[string][]string{"pprof::base": {"true"}},
		},
	}

	testcases := []struct {
		desc           string
		prof           *profile.Profile
		value, meanDiv func(v []int64) int64
		wantTotal      int64
	}{
		{
			desc: "no diff base, all positive values, index 1",
			prof: p1,
			value: func(v []int64) int64 {
				return v[0]
			},
			wantTotal: 3,
		},
		{
			desc: "no diff base, all positive values, index 2",
			prof: p1,
			value: func(v []int64) int64 {
				return v[1]
			},
			wantTotal: 111,
		},
		{
			desc: "no diff base, some negative values",
			prof: p2,
			value: func(v []int64) int64 {
				return v[1]
			},
			wantTotal: 111,
		},
		{
			desc: "diff base, some negative values",
			prof: p3,
			value: func(v []int64) int64 {
				return v[0]
			},
			wantTotal: 9111,
		},
	}

	for _, tc := range testcases {
		t.Run(tc.desc, func(t *testing.T) {
			if gotTotal := computeTotal(tc.prof, tc.value, tc.meanDiv); gotTotal != tc.wantTotal {
				t.Errorf("got total %d, want %v", gotTotal, tc.wantTotal)
			}
		})
	}
}

func TestPrintAssemblySymbolEnd(t *testing.T) {
	for _, tc := range []struct {
		name       string
		end        uint64
		sampleAddr uint64
		lastAddr   uint64
	}{
		{"last instruction", 0x1001, 0x1000, 0x1001},
		{"sample at last byte", 0x1001, 0x1001, 0x1001},
		{"single byte symbol", 0x1000, 0x1000, 0x1000},
		{"unknown symbol end", ^uint64(0), 0x1000, 0x1001},
	} {
		t.Run(tc.name, func(t *testing.T) {
			mapping := &profile.Mapping{ID: 1, File: "testbin", Start: 0x1000, Limit: 0x2000}
			function := &profile.Function{ID: 1, Name: "function"}
			location := &profile.Location{
				ID: 1, Mapping: mapping, Address: tc.sampleAddr,
				Line: []profile.Line{{Function: function}},
			}
			prof := &profile.Profile{
				SampleType: []*profile.ValueType{{Type: "samples", Unit: "count"}},
				Sample:     []*profile.Sample{{Location: []*profile.Location{location}, Value: []int64{7}}},
				Mapping:    []*profile.Mapping{mapping},
				Location:   []*profile.Location{location},
				Function:   []*profile.Function{function},
			}
			obj := assemblyObjTool{
				symbol: &plugin.Sym{Name: []string{"function"}, File: "testbin", Start: 0x1000, End: tc.end},
				insts:  []plugin.Inst{{Addr: tc.lastAddr, Text: "RET"}, {Addr: tc.lastAddr + 1, Text: "next symbol"}},
			}
			if tc.lastAddr > obj.symbol.Start {
				obj.insts = append([]plugin.Inst{{Addr: obj.symbol.Start, Text: "NOP"}}, obj.insts...)
			}
			rpt := NewDefault(prof, Options{OutputFormat: Dis, Symbol: regexp.MustCompile("function")})
			var buf bytes.Buffer
			if err := PrintAssembly(&buf, rpt, obj, -1); err != nil {
				t.Fatal(err)
			}
			output := buf.String()
			if want := fmt.Sprintf("%x: RET", tc.lastAddr); !strings.Contains(output, want) {
				t.Errorf("missing last instruction %q in:\n%s", want, output)
			}
			if tc.end != ^uint64(0) && strings.Contains(output, "next symbol") {
				t.Errorf("disassembled beyond the symbol:\n%s", output)
			}
			if want := fmt.Sprintf("%10s %10s (flat, cum)", "7", "7"); !strings.Contains(output, want) {
				t.Errorf("missing sample totals %q in:\n%s", want, output)
			}
			if tc.sampleAddr == tc.lastAddr {
				if want := fmt.Sprintf("%10s %10s %10x: RET", "7", "7", tc.lastAddr); !strings.Contains(output, want) {
					t.Errorf("missing last-instruction sample %q in:\n%s", want, output)
				}
			}
		})
	}
}

type assemblyObjTool struct {
	symbol *plugin.Sym
	insts  []plugin.Inst
}

func (o assemblyObjTool) Open(string, uint64, uint64, uint64, string) (plugin.ObjFile, error) {
	return assemblyObjFile{symbol: o.symbol}, nil
}

func (o assemblyObjTool) Disasm(_ string, start, end uint64, _ bool) ([]plugin.Inst, error) {
	var insts []plugin.Inst
	for _, inst := range o.insts {
		if inst.Addr >= start && inst.Addr < end {
			insts = append(insts, inst)
		}
	}
	return insts, nil
}

type assemblyObjFile struct {
	symbol *plugin.Sym
}

func (o assemblyObjFile) Symbols(*regexp.Regexp, uint64) ([]*plugin.Sym, error) {
	return []*plugin.Sym{o.symbol}, nil
}

func (assemblyObjFile) ObjAddr(addr uint64) (uint64, error)       { return addr, nil }
func (assemblyObjFile) Close() error                              { return nil }
func (o assemblyObjFile) Name() string                            { return o.symbol.File }
func (assemblyObjFile) BuildID() string                           { return "" }
func (assemblyObjFile) SourceLine(uint64) ([]plugin.Frame, error) { return nil, nil }

func TestPrintAssemblyErrorMessage(t *testing.T) {
	profile := readProfile(filepath.Join("testdata", "sample.cpu"), t)

	for _, tc := range []struct {
		desc   string
		symbol string
		want   string
	}{
		{
			desc:   "no matched symbol in binary",
			symbol: "symbol-not-exist",
			want:   "no matches found for regexp symbol-not-exist",
		},
		{
			desc:   "no matched address in binary",
			symbol: "0xffffaaaa",
			want:   "no matches found for address 0xffffaaaa",
		},
		{
			desc:   "matched address in binary but not in the profile",
			symbol: "0x400000",
			want:   "address 0x400000 found in binary, but the corresponding symbols do not have samples in the profile",
		},
	} {
		rpt := New(
			profile.Copy(),
			&Options{
				OutputFormat: List,
				Symbol:       regexp.MustCompile(tc.symbol),
				SampleValue: func(v []int64) int64 {
					return v[1]
				},
				SampleUnit: profile.SampleType[1].Unit,
			},
		)

		if err := PrintAssembly(os.Stdout, rpt, &binutils.Binutils{}, -1); err == nil || err.Error() != tc.want {
			t.Errorf(`Got "%v", want %q`, err, tc.want)
		}
	}
}

func TestDocURL(t *testing.T) {
	type testCase struct {
		input string
		want  string
	}
	for name, c := range map[string]testCase{
		"empty":    {"", ""},
		"http":     {"http://example.com/pprof-help", "http://example.com/pprof-help"},
		"https":    {"https://example.com/pprof-help", "https://example.com/pprof-help"},
		"relative": {"/foo", ""},
		"nonhttp":  {"mailto:nobody@example.com", ""},
	} {
		t.Run(name, func(t *testing.T) {
			profile := testProfile.Copy()
			profile.DocURL = c.input
			rpt := New(profile, &Options{
				OutputFormat: Dot,
				Symbol:       regexp.MustCompile(`.`),
				TrimPath:     "/some/path",
				SampleValue:  func(v []int64) int64 { return v[1] },
				SampleUnit:   testProfile.SampleType[1].Unit,
			})
			if got := rpt.DocURL(); got != c.want {
				t.Errorf("bad doc URL %q, expecting %q", got, c.want)
			}
		})
	}
}

func TestDocURLInLabels(t *testing.T) {
	const url = "http://example.com/pprof-help"
	profile := testProfile.Copy()
	profile.DocURL = url
	rpt := New(profile, &Options{
		OutputFormat: Text,
		Symbol:       regexp.MustCompile(`.`),
		TrimPath:     "/some/path",
		SampleValue:  func(v []int64) int64 { return v[1] },
		SampleUnit:   testProfile.SampleType[1].Unit,
	})

	labels := fmt.Sprintf("%v", ProfileLabels(rpt))
	if !strings.Contains(labels, url) {
		t.Errorf("expected URL %q not found in %s", url, labels)
	}
}

func TestProfileLabels(t *testing.T) {
	// Force the local timezone to UTC for the duration of this function to get a
	// predictable result out of timezone printing.
	defer func(prev *time.Location) { time.Local = prev }(time.Local)
	time.Local = time.UTC

	profile := testProfile.Copy()
	profile.TimeNanos = time.Unix(131, 0).UnixNano()
	rpt := New(profile, &Options{
		SampleValue: func(v []int64) int64 { return v[1] },
	})

	const want = "Time: 1970-01-01 00:02:11 UTC"
	if labels := ProfileLabels(rpt); !slices.Contains(labels, want) {
		t.Errorf("wanted to find a label containing %q, but found none in %v", want, labels)
	}
}

func BenchmarkReportNewTrimmedGraph(b *testing.B) {
	data := proftest.LargeProfile(b)
	prof, err := profile.Parse(bytes.NewBuffer(data))
	if err != nil {
		b.Fatal(err)
	}
	rep := NewDefault(prof, Options{})

	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		g, _, _, _ := rep.newTrimmedGraph()
		if g == nil {
			b.Fatal("empty graph")
		}
	}
}
