// Copyright 2020 Google Inc. All Rights Reserved.
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
	"reflect"
	"testing"
)

func countSampleLocations(p *Profile) []int {
	var locs []int
	for _, s := range p.Sample {
		locs = append(locs, len(s.Location))
	}
	return locs
}

func TestAddRootLeafTagsAdLocations(t *testing.T) {
	// Perform several forms of pseudo location addition on the test
	// profile.
	const notfound = "notfound"
	const key1 = "key1"
	const key2 = "key2"
	type pseudoTestcase struct {
		name             string
		rootTag, leafTag string
		wr, wl           bool
		locs             []int
	}
	for _, tc := range []pseudoTestcase{
		// Empty root and leaf tags.
		{"EmptyRootAndLeafTags", "", "", false, false, []int{1, 2, 2, 2, 2}},
		// Root and leaf tags not present in any of the samples.
		{"TagsNotFound", notfound, notfound, false, false, []int{1, 2, 2, 2, 2}},
		// One root tag.
		{"AddRootTag", key1, "", true, false, []int{2, 3, 3, 3, 3}},
		// One leaf tag.
		{"AddLeafTag", "", key2, false, true, []int{2, 2, 3, 3, 3}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			prof := testProfile1.Copy()
			gr, gl := prof.AddRootLeafTagsAsLocations(tc.rootTag, tc.leafTag)
			if gr != tc.wr || gl != tc.wl {
				t.Fatalf("AddRootLeafTagsAsLocations(%s, %s)=%v,%v want %v,%v", tc.rootTag, tc.leafTag, gr, gl, tc.wr, tc.wl)
			}
			// Test that samples with matching tag have expected number of locations.
			if locs := countSampleLocations(prof); !reflect.DeepEqual(locs, tc.locs) {
				t.Errorf("locs = %v, want %v", locs, tc.locs)
			}
		})
	}
}
