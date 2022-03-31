package driver

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/pprof/internal/proftest"
	"github.com/google/pprof/profile"
)

const mainBinary = "/bin/main"

var cpuF = []*profile.Function{
	{ID: 1, Name: "main", SystemName: "main", Filename: "main.c"},
	{ID: 2, Name: "foo", SystemName: "foo", Filename: "foo.c"},
	{ID: 3, Name: "foo_caller", SystemName: "foo_caller", Filename: "foo.c"},
	{ID: 4, Name: "bar", SystemName: "bar", Filename: "bar.c"},
}

var cpuM = []*profile.Mapping{
	{
		ID:              1,
		Start:           0x10000,
		Limit:           0x40000,
		File:            mainBinary,
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
	{
		ID:              2,
		Start:           0x1000,
		Limit:           0x4000,
		File:            "/lib/lib.so",
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
}

var cpuL = []*profile.Location{
	{
		ID:      1000,
		Mapping: cpuM[1],
		Address: 0x1000,
		Line: []profile.Line{
			{Function: cpuF[0], Line: 1},
		},
	},
	{
		ID:      2000,
		Mapping: cpuM[0],
		Address: 0x2000,
		Line: []profile.Line{
			{Function: cpuF[1], Line: 2},
			{Function: cpuF[2], Line: 1},
		},
	},
	{
		ID:      3000,
		Mapping: cpuM[0],
		Address: 0x3000,
		Line: []profile.Line{
			{Function: cpuF[1], Line: 2},
			{Function: cpuF[2], Line: 1},
		},
	},
	{
		ID:      3001,
		Mapping: cpuM[0],
		Address: 0x3001,
		Line: []profile.Line{
			{Function: cpuF[2], Line: 2},
		},
	},
	{
		ID:      3002,
		Mapping: cpuM[0],
		Address: 0x3002,
		Line: []profile.Line{
			{Function: cpuF[2], Line: 3},
		},
	},
	{
		ID:      3003,
		Mapping: cpuM[0],
		Address: 0x3003,
		Line: []profile.Line{
			{Function: cpuF[3], Line: 1},
		},
	},
}

var testProfile1 = &profile.Profile{
	TimeNanos:     10000,
	PeriodType:    &profile.ValueType{Type: "cpu", Unit: "milliseconds"},
	Period:        1,
	DurationNanos: 10e9,
	SampleType: []*profile.ValueType{
		{Type: "samples", Unit: "count"},
		{Type: "cpu", Unit: "milliseconds"},
	},
	Sample: []*profile.Sample{
		{
			Location: []*profile.Location{cpuL[0]},
			Value:    []int64{1000, 1000},
			Label: map[string][]string{
				"key1": {"tag1"},
				"key2": {"tag1"},
			},
		},
		{
			Location: []*profile.Location{cpuL[1], cpuL[0]},
			Value:    []int64{100, 100},
			Label: map[string][]string{
				"key1": {"tag2"},
				"key3": {"tag2"},
			},
		},
		{
			Location: []*profile.Location{cpuL[2], cpuL[0]},
			Value:    []int64{10, 10},
			Label: map[string][]string{
				"key1": {"tag3"},
				"key2": {"tag2"},
			},
			NumLabel: map[string][]int64{
				"allocations": {1024},
			},
			NumUnit: map[string][]string{
				"allocations": {""},
			},
		},
		{
			Location: []*profile.Location{cpuL[3], cpuL[0]},
			Value:    []int64{10000, 10000},
			Label: map[string][]string{
				"key1": {"tag4"},
				"key2": {"tag1"},
			},
			NumLabel: map[string][]int64{
				"allocations": {1024, 2048},
			},
			NumUnit: map[string][]string{
				"allocations": {"bytes", "b"},
			},
		},
		{
			Location: []*profile.Location{cpuL[4], cpuL[0]},
			Value:    []int64{1, 1},
			Label: map[string][]string{
				"key1": {"tag4"},
				"key2": {"tag1", "tag5"},
			},
			NumLabel: map[string][]int64{
				"allocations": {1024, 1},
			},
			NumUnit: map[string][]string{
				"allocations": {"byte", "kilobyte"},
			},
		},
		{
			Location: []*profile.Location{cpuL[5], cpuL[0]},
			Value:    []int64{200, 200},
			NumLabel: map[string][]int64{
				"allocations": {1024},
			},
		},
	},
	Location: cpuL,
	Function: cpuF,
	Mapping:  cpuM,
}

func TestAddLabelNodesMatchBooleans(t *testing.T) {
	type addLabelNodesTestcase struct {
		name             string
		tagroot, tagleaf []string
		outputUnit       string
		rootm, leafm     bool
		// wantSampleFuncs contains expected stack functions and sample value after
		// adding nodes, in the same order as in the profile. The format is as
		// returned by stackCollapse function, which is "callee caller: <num>".
		wantSampleFuncs []string
	}
	for _, tc := range []addLabelNodesTestcase{
		{
			name: "Without tagroot or tagleaf, add no extra nodes, and should not match",
			wantSampleFuncs: []string{
				"main(main.c) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c) 10",
				"main(main.c);foo_caller(foo.c) 10000",
				"main(main.c);foo_caller(foo.c) 1",
				"main(main.c);bar(bar.c) 200",
			},
		},
		{
			name:    "Keys that aren't found add empty nodes, and should not match",
			tagroot: []string{"key404"},
			tagleaf: []string{"key404"},
			wantSampleFuncs: []string{
				"(key404);main(main.c);(key404) 1000",
				"(key404);main(main.c);foo(foo.c);foo_caller(foo.c);(key404) 100",
				"(key404);main(main.c);foo(foo.c);foo_caller(foo.c);(key404) 10",
				"(key404);main(main.c);foo_caller(foo.c);(key404) 10000",
				"(key404);main(main.c);foo_caller(foo.c);(key404) 1",
				"(key404);main(main.c);bar(bar.c);(key404) 200",
			},
		},
		{
			name:    "tagroot adds nodes for key1 and reports a match",
			tagroot: []string{"key1"},
			rootm:   true,
			wantSampleFuncs: []string{
				"tag1(key1);main(main.c) 1000",
				"tag2(key1);main(main.c);foo(foo.c);foo_caller(foo.c) 100",
				"tag3(key1);main(main.c);foo(foo.c);foo_caller(foo.c) 10",
				"tag4(key1);main(main.c);foo_caller(foo.c) 10000",
				"tag4(key1);main(main.c);foo_caller(foo.c) 1",
				"(key1);main(main.c);bar(bar.c) 200",
			},
		},
		{
			name:    "tagroot adds nodes for key2 and reports a match",
			tagroot: []string{"key2"},
			rootm:   true,
			wantSampleFuncs: []string{
				"tag1(key2);main(main.c) 1000",
				"(key2);main(main.c);foo(foo.c);foo_caller(foo.c) 100",
				"tag2(key2);main(main.c);foo(foo.c);foo_caller(foo.c) 10",
				"tag1(key2);main(main.c);foo_caller(foo.c) 10000",
				"tag1,tag5(key2);main(main.c);foo_caller(foo.c) 1",
				"(key2);main(main.c);bar(bar.c) 200",
			},
		},
		{
			name:    "tagleaf adds nodes for key1 and reports a match",
			tagleaf: []string{"key1"},
			leafm:   true,
			wantSampleFuncs: []string{
				"main(main.c);tag1(key1) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c);tag2(key1) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c);tag3(key1) 10",
				"main(main.c);foo_caller(foo.c);tag4(key1) 10000",
				"main(main.c);foo_caller(foo.c);tag4(key1) 1",
				"main(main.c);bar(bar.c);(key1) 200",
			},
		},
		{
			name:    "tagleaf adds nodes for key3 and reports a match",
			tagleaf: []string{"key3"},
			leafm:   true,
			wantSampleFuncs: []string{
				"main(main.c);(key3) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c);tag2(key3) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c);(key3) 10",
				"main(main.c);foo_caller(foo.c);(key3) 10000",
				"main(main.c);foo_caller(foo.c);(key3) 1",
				"main(main.c);bar(bar.c);(key3) 200",
			},
		},
		{
			name:    "tagroot adds nodes for key1,key2 in order and reports a match",
			tagroot: []string{"key1", "key2"},
			rootm:   true,
			wantSampleFuncs: []string{
				"tag1(key1);tag1(key2);main(main.c) 1000",
				"tag2(key1);(key2);main(main.c);foo(foo.c);foo_caller(foo.c) 100",
				"tag3(key1);tag2(key2);main(main.c);foo(foo.c);foo_caller(foo.c) 10",
				"tag4(key1);tag1(key2);main(main.c);foo_caller(foo.c) 10000",
				"tag4(key1);tag1,tag5(key2);main(main.c);foo_caller(foo.c) 1",
				"(key1);(key2);main(main.c);bar(bar.c) 200",
			},
		},
		{
			name:    "tagleaf adds nodes for key1,key2 in order and reports a match",
			tagleaf: []string{"key1", "key2"},
			leafm:   true,
			wantSampleFuncs: []string{
				"main(main.c);tag1(key1);tag1(key2) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c);tag2(key1);(key2) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c);tag3(key1);tag2(key2) 10",
				"main(main.c);foo_caller(foo.c);tag4(key1);tag1(key2) 10000",
				"main(main.c);foo_caller(foo.c);tag4(key1);tag1,tag5(key2) 1",
				"main(main.c);bar(bar.c);(key1);(key2) 200",
			},
		},
		{
			name:    "Numeric units are added with units with tagleaf",
			tagleaf: []string{"allocations"},
			leafm:   true,
			wantSampleFuncs: []string{
				"main(main.c);(allocations) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c);(allocations) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c);1024(allocations) 10",
				"main(main.c);foo_caller(foo.c);1024B,2048B(allocations) 10000",
				"main(main.c);foo_caller(foo.c);1024B,1024B(allocations) 1",
				"main(main.c);bar(bar.c);1024(allocations) 200",
			},
		},
		{
			name:    "Numeric units are added with units with tagroot",
			tagroot: []string{"allocations"},
			rootm:   true,
			wantSampleFuncs: []string{
				"(allocations);main(main.c) 1000",
				"(allocations);main(main.c);foo(foo.c);foo_caller(foo.c) 100",
				"1024(allocations);main(main.c);foo(foo.c);foo_caller(foo.c) 10",
				"1024B,2048B(allocations);main(main.c);foo_caller(foo.c) 10000",
				"1024B,1024B(allocations);main(main.c);foo_caller(foo.c) 1",
				"1024(allocations);main(main.c);bar(bar.c) 200",
			},
		},
		{
			name:       "Numeric labels are formatted according to outputUnit",
			outputUnit: "kB",
			tagleaf:    []string{"allocations"},
			leafm:      true,
			wantSampleFuncs: []string{
				"main(main.c);(allocations) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c);(allocations) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c);1024(allocations) 10",
				"main(main.c);foo_caller(foo.c);1kB,2kB(allocations) 10000",
				"main(main.c);foo_caller(foo.c);1kB,1kB(allocations) 1",
				"main(main.c);bar(bar.c);1024(allocations) 200",
			},
		},
		{
			name:    "Numeric units with no units are handled properly by tagleaf",
			tagleaf: []string{"allocations"},
			leafm:   true,
			wantSampleFuncs: []string{
				"main(main.c);(allocations) 1000",
				"main(main.c);foo(foo.c);foo_caller(foo.c);(allocations) 100",
				"main(main.c);foo(foo.c);foo_caller(foo.c);1024(allocations) 10",
				"main(main.c);foo_caller(foo.c);1024B,2048B(allocations) 10000",
				"main(main.c);foo_caller(foo.c);1024B,1024B(allocations) 1",
				"main(main.c);bar(bar.c);1024(allocations) 200",
			},
		},
	} {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			p := testProfile1.Copy()
			rootm, leafm := addLabelNodes(p, tc.tagroot, tc.tagleaf, tc.outputUnit)
			if rootm != tc.rootm {
				t.Errorf("Got rootm=%v, want=%v", rootm, tc.rootm)
			}
			if leafm != tc.leafm {
				t.Errorf("Got leafm=%v, want=%v", leafm, tc.leafm)
			}
			if got, want := strings.Join(stackCollapse(p), "\n")+"\n", strings.Join(tc.wantSampleFuncs, "\n")+"\n"; got != want {
				diff, err := proftest.Diff([]byte(want), []byte(got))
				if err != nil {
					t.Fatalf("Failed to get diff: %v", err)
				}
				t.Errorf("Profile samples got diff(want->got):\n%s", diff)
			}
		})
	}
}

// stackCollapse returns a slice of strings where each string represents one
// profile sample in Brendan Gregg's "Folded Stacks" format:
// "<root_fn>(filename);<fun2>(filename);<leaf_fn>(filename) <value>". This
// allows the expected values for test cases to be specified in human-readable
// strings.
func stackCollapse(p *profile.Profile) []string {
	var ret []string
	for _, s := range p.Sample {
		var funcs []string
		for i := range s.Location {
			loc := s.Location[len(s.Location)-1-i]
			for _, line := range loc.Line {
				funcs = append(funcs, fmt.Sprintf("%s(%s)", line.Function.Name, line.Function.Filename))
			}
		}
		ret = append(ret, fmt.Sprintf("%s %d", strings.Join(funcs, ";"), s.Value[0]))
	}
	return ret
}
