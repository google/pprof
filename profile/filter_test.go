// Copyright 2018 Google Inc. All Rights Reserved.
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
		SampleType:    []*ValueType{{Type: "samples", Unit: "count"}},
		Mapping:       mappings,
		Function:      functions,
		Location:      locations,
		Sample: []*Sample{
			{Value: []int64{1}, Location: []*Location{locations[0], locations[1], locations[2], locations[3]}},
			{Value: []int64{2}, Location: []*Location{locations[4], locations[5], locations[1], locations[6]}},
			{Value: []int64{3}, Location: []*Location{locations[7], locations[8]}},
			{Value: []int64{4}, Location: []*Location{locations[9], locations[4], locations[10], locations[7]}},
		},
	}

	type FilterMatch struct {
		fm, im, sm, hm bool
	}

	allPaths := [][]int{
		{0, 1, 2, 3},
		{4, 5, 1, 6},
		{7, 8},
		{9, 4, 10, 7},
	}
	allValues := []int64{1, 2, 3, 4}

	for _, testCase := range []struct {
		// name is the name of the test case.
		name string
		// These are the inputs to FilterSamplesByName().
		focus, ignore, hide, show *regexp.Regexp
		// wantMatch contains the expected return values from FilterSamplesByName().
		wantMatch FilterMatch
		// wantPaths contains the location indices of expected paths, indexed by
		// sample and then frame.
		// I.e. wantPaths[sampleIndex][frameIndex] = locationIndex.
		wantPaths [][]int
		// wantValues contains the expected values, indexed by sample.
		// I.e. wantValues[sampleIndex] = sampleValue.
		wantValues []int64
		// filterLocations specifes whether to remove functions from unused
		// locations. This necessary because the focus and ignore filters do not
		// remove functions, but show and hide do.
		filterLocations bool
	}{
		// No Filters
		{
			name:       "empty filters keep all frames",
			wantMatch:  FilterMatch{fm: true},
			wantPaths:  allPaths,
			wantValues: allValues,
		},
		// Focus
		{
			name:  "focus with no matches",
			focus: regexp.MustCompile("unknown"),
		},
		{
			name:      "focus matches function names",
			focus:     regexp.MustCompile("fun1"),
			wantMatch: FilterMatch{fm: true},
			wantPaths: [][]int{
				{0, 1, 2, 3},
				{4, 5, 1, 6},
				{9, 4, 10, 7},
			},
			wantValues: []int64{1, 2, 4},
		},
		{
			name:      "focus matches file names",
			focus:     regexp.MustCompile("file1"),
			wantMatch: FilterMatch{fm: true},
			wantPaths: [][]int{
				{0, 1, 2, 3},
				{4, 5, 1, 6},
				{9, 4, 10, 7},
			},
			wantValues: []int64{1, 2, 4},
		},
		{
			name:      "focus matches mapping names",
			focus:     regexp.MustCompile("map1"),
			wantMatch: FilterMatch{fm: true},
			wantPaths: [][]int{
				{9, 4, 10, 7},
			},
			wantValues: []int64{4},
		},
		// Ignore
		{
			name:       "ignore with no matches",
			ignore:     regexp.MustCompile("unknown"),
			wantMatch:  FilterMatch{fm: true},
			wantPaths:  allPaths,
			wantValues: allValues,
		},
		{
			name:      "ignore matches function names",
			ignore:    regexp.MustCompile("fun1"),
			wantMatch: FilterMatch{fm: true, im: true},
			wantPaths: [][]int{
				{7, 8},
			},
			wantValues: []int64{3},
		},
		{
			name:      "ignore matches file names",
			ignore:    regexp.MustCompile("file1"),
			wantMatch: FilterMatch{fm: true, im: true},
			wantPaths: [][]int{
				{7, 8},
			},
			wantValues: []int64{3},
		},
		{
			name:      "ignore matches mapping names",
			ignore:    regexp.MustCompile("map1"),
			wantMatch: FilterMatch{fm: true, im: true},
			wantPaths: [][]int{
				{0, 1, 2, 3},
				{4, 5, 1, 6},
				{7, 8},
			},
			wantValues: []int64{1, 2, 3},
		},
		// Show
		{
			name:            "show with no matches",
			show:            regexp.MustCompile("unknown"),
			wantMatch:       FilterMatch{fm: true},
			filterLocations: true,
		},
		{
			name:      "show matches function names",
			show:      regexp.MustCompile("fun1|fun2"),
			wantMatch: FilterMatch{fm: true, sm: true},
			wantPaths: [][]int{
				{1, 2},
				{1},
				{10},
			},
			wantValues:      []int64{1, 2, 4},
			filterLocations: true,
		},
		{
			name:      "show matches file names",
			show:      regexp.MustCompile("file1|file3"),
			wantMatch: FilterMatch{fm: true, sm: true},
			wantPaths: [][]int{
				{1, 3},
				{1},
				{10},
			},
			wantValues:      []int64{1, 2, 4},
			filterLocations: true,
		},
		{
			name:      "show matches mapping names",
			show:      regexp.MustCompile("map1"),
			wantMatch: FilterMatch{fm: true, sm: true},
			wantPaths: [][]int{
				{10},
			},
			wantValues:      []int64{4},
			filterLocations: true,
		},
		// Hide
		{
			name:       "hide with no matches",
			hide:       regexp.MustCompile("unknown"),
			wantMatch:  FilterMatch{fm: true},
			wantPaths:  allPaths,
			wantValues: allValues,
		},
		{
			name:      "hide matches function names",
			hide:      regexp.MustCompile("fun1|fun2"),
			wantMatch: FilterMatch{fm: true, hm: true},
			wantPaths: [][]int{
				{0, 3},
				{4, 5, 6},
				{7, 8},
				{9, 4, 7},
			},
			wantValues:      []int64{1, 2, 3, 4},
			filterLocations: true,
		},
		{
			name:      "hide matches file names",
			hide:      regexp.MustCompile("file1|file3"),
			wantMatch: FilterMatch{fm: true, hm: true},
			wantPaths: [][]int{
				{0, 2},
				{4, 5, 6},
				{7, 8},
				{9, 4, 7},
			},
			wantValues:      []int64{1, 2, 3, 4},
			filterLocations: true,
		},
		{
			name:      "hide matches mapping names",
			hide:      regexp.MustCompile("map1"),
			wantMatch: FilterMatch{fm: true, hm: true},
			wantPaths: [][]int{
				{0, 1, 2, 3},
				{4, 5, 1, 6},
				{7, 8},
				{9, 4, 7},
			},
			wantValues:      []int64{1, 2, 3, 4},
			filterLocations: true,
		},
		// Compound filters
		{
			name:      "hides a stack matched by both focus and ignore",
			focus:     regexp.MustCompile("fun1|fun7"),
			ignore:    regexp.MustCompile("fun1"),
			wantMatch: FilterMatch{fm: true, im: true},
			wantPaths: [][]int{
				{7, 8},
			},
			wantValues: []int64{3},
		},
		{
			name:      "hides a function if both show and hide match it",
			show:      regexp.MustCompile("fun1"),
			hide:      regexp.MustCompile("fun10"),
			wantMatch: FilterMatch{fm: true, sm: true, hm: true},
			wantPaths: [][]int{
				{1},
				{1},
			},
			wantValues:      []int64{1, 2},
			filterLocations: true,
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			gotProfile := profile.Copy()
			fm, im, hm, sm := gotProfile.FilterSamplesByName(testCase.focus, testCase.ignore, testCase.hide, testCase.show)
			gotMatch := FilterMatch{fm: fm, im: im, hm: hm, sm: sm}
			gotStr := gotProfile.String()

			if gotMatch != testCase.wantMatch {
				t.Errorf("match got %+v want %+v", gotMatch, testCase.wantMatch)
			}

			wantSamples, wantLocations := makeFilteredSamples(testCase.wantPaths, testCase.wantValues, testCase.filterLocations, locations)
			wantProfile := profile.Copy()
			wantProfile.Sample = wantSamples
			wantProfile.Location = wantLocations
			wantStr := wantProfile.String()

			if wantStr != gotStr {
				diff, err := proftest.Diff([]byte(wantStr), []byte(gotStr))
				if err != nil {
					t.Fatalf("failed to get diff: %v", err)
				} else {
					t.Errorf("filtered profile got diff(want->got):\n%s", diff)
				}
			}
		})
	}
}

// makeFilteredSamples constructs  filtered profile samples and locations from
// basic inputs to simplify the specification of expected values in filter test
// cases. paths contains the location indices, indexed by sample and frame, of
// the filtered samples. values contains the values, indexed by sample, of the
// filtered samples. filterLocations is whether to remove function info from
// unused locations. locations is the slice of locations from the original,
// unfiltered profile, used to derive the new filtered locations.
func makeFilteredSamples(paths [][]int, values []int64, filterLocations bool, locations []*Location) ([]*Sample, []*Location) {
	samples := make([]*Sample, len(paths))
	usedLocationIndices := map[int]bool{}

	for sampleIndex, path := range paths {
		sample := &Sample{
			Value:    []int64{values[sampleIndex]},
			Location: make([]*Location, len(path)),
		}
		for frameIndex, locationIndex := range path {
			sample.Location[frameIndex] = locations[locationIndex]
			usedLocationIndices[locationIndex] = true
		}
		samples[sampleIndex] = sample
	}

	filteredLocations := locations
	if filterLocations {
		filteredLocations = nil
		for i, _ := range locations {
			if usedLocationIndices[i] {
				filteredLocations = append(filteredLocations, locations[i])
			} else {
				newLocation := *locations[i]
				newLocation.Line = nil
				filteredLocations = append(filteredLocations, &newLocation)
			}
		}
	}

	return samples, filteredLocations
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
