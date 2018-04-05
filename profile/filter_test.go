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

package profile

import (
	"regexp"
	"testing"

	"github.com/google/pprof/internal/proftest"
)

func TestFilter(t *testing.T) {
	// Perform several forms of filtering on the test profile.

	type filterTestcase struct {
		focus, ignore, hide, show *regexp.Regexp
		fm, im, hm, hnm           bool
	}

	for tx, tc := range []filterTestcase{
		{
			fm: true, // nil focus matches every sample
		},
		{
			focus: regexp.MustCompile("notfound"),
		},
		{
			ignore: regexp.MustCompile("foo.c"),
			fm:     true,
			im:     true,
		},
		{
			hide: regexp.MustCompile("lib.so"),
			fm:   true,
			hm:   true,
		},
		{
			show: regexp.MustCompile("foo.c"),
			fm:   true,
			hnm:  true,
		},
		{
			show: regexp.MustCompile("notfound"),
			fm:   true,
		},
	} {
		prof := *testProfile1.Copy()
		gf, gi, gh, gnh := prof.FilterSamplesByName(tc.focus, tc.ignore, tc.hide, tc.show)
		if gf != tc.fm {
			t.Errorf("Filter #%d, got fm=%v, want %v", tx, gf, tc.fm)
		}
		if gi != tc.im {
			t.Errorf("Filter #%d, got im=%v, want %v", tx, gi, tc.im)
		}
		if gh != tc.hm {
			t.Errorf("Filter #%d, got hm=%v, want %v", tx, gh, tc.hm)
		}
		if gnh != tc.hnm {
			t.Errorf("Filter #%d, got hnm=%v, want %v", tx, gnh, tc.hnm)
		}
	}
}

func TestFilter2(t *testing.T) {
	mappings := []*Mapping{
		{ID: 1, Start: 0x10000, Limit: 0x40000, File: "map0", HasFunctions: true, HasFilenames: true, HasLineNumbers: true, HasInlineFrames: true},
		{ID: 2, Start: 0x50000, Limit: 0x70000, File: "map1", HasFunctions: true, HasFilenames: true, HasLineNumbers: true, HasInlineFrames: true},
	}
	functions := []*Function{
		{ID: 1, Name: "fun0", SystemName: "fun0", Filename: "file0"},
		{ID: 2, Name: "fun1", SystemName: "fun1", Filename: "file1"},
		{ID: 3, Name: "fun2", SystemName: "fun2", Filename: "file2"},
		{ID: 4, Name: "fun3", SystemName: "fun3", Filename: "file3"},
		{ID: 5, Name: "fun4", SystemName: "fun4", Filename: "file4"},
		{ID: 6, Name: "fun5", SystemName: "fun5", Filename: "file5"},
		{ID: 7, Name: "fun6", SystemName: "fun6", Filename: "file6"},
		{ID: 8, Name: "fun7", SystemName: "fun7", Filename: "file7"},
		{ID: 9, Name: "fun8", SystemName: "fun8", Filename: "file8"},
		{ID: 10, Name: "fun9", SystemName: "fun9", Filename: "file9"},
		{ID: 11, Name: "fun10", SystemName: "fun10", Filename: "file10"},
	}
	locations := []*Location{
		{ID: 1, Mapping: mappings[0], Address: 0x1000, Line: []Line{{Function: functions[0], Line: 1}}},
		{ID: 2, Mapping: mappings[0], Address: 0x2000, Line: []Line{{Function: functions[1], Line: 1}}},
		{ID: 3, Mapping: mappings[0], Address: 0x3000, Line: []Line{{Function: functions[2], Line: 1}}},
		{ID: 4, Mapping: mappings[0], Address: 0x4000, Line: []Line{{Function: functions[3], Line: 1}}},
		{ID: 5, Mapping: mappings[0], Address: 0x5000, Line: []Line{{Function: functions[4], Line: 1}}},
		{ID: 6, Mapping: mappings[0], Address: 0x6000, Line: []Line{{Function: functions[5], Line: 1}}},
		{ID: 7, Mapping: mappings[0], Address: 0x7000, Line: []Line{{Function: functions[6], Line: 1}}},
		{ID: 8, Mapping: mappings[0], Address: 0x8000, Line: []Line{{Function: functions[7], Line: 1}}},
		{ID: 9, Mapping: mappings[0], Address: 0x9000, Line: []Line{{Function: functions[8], Line: 1}}},
		{ID: 10, Mapping: mappings[0], Address: 0x10000, Line: []Line{{Function: functions[9], Line: 1}}},
		{ID: 11, Mapping: mappings[1], Address: 0x11000, Line: []Line{{Function: functions[10], Line: 1}}},
	}
	profile := &Profile{
		TimeNanos:     10000,
		PeriodType:    &ValueType{Type: "cpu", Unit: "milliseconds"},
		Period:        1,
		DurationNanos: 10e9,
		SampleType: []*ValueType{{Type: "samples", Unit: "count"}},
		Mapping:  mappings,
		Function: functions,
		Location: locations,
		Sample: []*Sample{
			{Value: []int64{1}, Location: []*Location{locations[0], locations[1], locations[2], locations[3]}},
			{Value: []int64{2}, Location: []*Location{locations[4], locations[5], locations[1], locations[6]}},
			{Value: []int64{3}, Location: []*Location{locations[7], locations[8]}},
			{Value: []int64{4}, Location: []*Location{locations[9], locations[4], locations[10], locations[7]}},
		},
	}

	for _, testCase := range []struct{
		name string
		focus, ignore, hide, show *regexp.Regexp
		fm, im, hm, hnm           bool
		wantPaths [][]int // expected paths, wantPaths[sampleIndex][frameIndex] = locationIndex
		wantValues []int64
	}{
		{
			fm: true, // nil focus matches every sample
			wantPaths: [][]int{
				{0, 1, 2, 3},
				{4, 5, 1, 6},
				{7, 8},
				{9, 4, 10, 7},
			},
			wantValues: []int64{1, 2, 3, 4},
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			gotProfile := profile.Copy()
			gotProfile.FilterSamplesByName(testCase.focus, testCase.ignore, testCase.hide, testCase.show)
			gotStr := gotProfile.String()

			wantProfile := profile.Copy()
			wantProfile.Sample = makeSamples(testCase.wantPaths, testCase.wantValues, locations)
			wantStr := wantProfile.String()

			if wantStr != gotStr {
				diff, err := proftest.Diff([]byte(wantStr), []byte(gotStr))
				if err != nil {
					t.Fatalf("failed to get diff: %v", err)
				}
				t.Errorf("filtered profile got diff(want->got): \n%s", diff)
			}
		})
	}
}

func makeSamples(paths [][]int, values []int64, locations []*Location) []*Sample {
	samples := make([]*Sample, len(paths))
	for sampleIndex, path := range paths {
		sample := &Sample{
			Value: []int64{values[sampleIndex]},
			Location: make([]*Location, len(path)),
		}
		for frameIndex, locationIndex := range path {
			sample.Location[frameIndex] = locations[locationIndex]
		}
		samples[sampleIndex] = sample
	}
	return samples
}

func TestTagFilter(t *testing.T) {
	// Perform several forms of tag filtering on the test profile.

	type filterTestcase struct {
		include, exclude *regexp.Regexp
		im, em           bool
		count            int
	}

	countTags := func(p *Profile) map[string]bool {
		tags := make(map[string]bool)

		for _, s := range p.Sample {
			for l := range s.Label {
				tags[l] = true
			}
			for l := range s.NumLabel {
				tags[l] = true
			}
		}
		return tags
	}

	for tx, tc := range []filterTestcase{
		{nil, nil, true, false, 3},
		{regexp.MustCompile("notfound"), nil, false, false, 0},
		{regexp.MustCompile("key1"), nil, true, false, 1},
		{nil, regexp.MustCompile("key[12]"), true, true, 1},
	} {
		prof := testProfile1.Copy()
		gim, gem := prof.FilterTagsByName(tc.include, tc.exclude)
		if gim != tc.im {
			t.Errorf("Filter #%d, got include match=%v, want %v", tx, gim, tc.im)
		}
		if gem != tc.em {
			t.Errorf("Filter #%d, got exclude match=%v, want %v", tx, gem, tc.em)
		}
		if tags := countTags(prof); len(tags) != tc.count {
			t.Errorf("Filter #%d, got %d tags[%v], want %d", tx, len(tags), tags, tc.count)
		}
	}
}
