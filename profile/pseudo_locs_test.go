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

func countSampleLocs(p *Profile) []int {
	var locs []int
	for _, s := range p.Sample {
		locs = append(locs, len(s.Location))
	}
	return locs
}

func TestAddPseudoLocs(t *testing.T) {
	// Perform several forms of pseudo location addition on the test
	// profile.
	notfound := "notfound"
	key1 := "key1"
	key2 := "key2"
	type pseudoTestcase struct {
		rootTags, leafTags []*string
		wr, wl             bool
		locs               []int
	}
	for tx, tc := range []pseudoTestcase{
		// Empty root and leaf tags.
		{nil, nil, false, false, []int{1, 2, 2, 2, 2}},
		// Root and leaf tags not present in any of the samples.
		{[]*string{&notfound}, []*string{&notfound}, false, false, []int{1, 2, 2, 2, 2}},
		// One root tag.
		{[]*string{&key1}, nil, true, false, []int{2, 3, 3, 3, 3}},
		// One leaf tag.
		{nil, []*string{&key2}, false, true, []int{2, 2, 3, 3, 3}},
	} {
		prof := testProfile1.Copy()
		gr, gl := prof.AddPseudoLocs(tc.rootTags, tc.leafTags)
		if gr != tc.wr {
			t.Errorf("PseudoLocs #%d, got root tag=%v, want %v", tx, gr, tc.wr)
		}
		if gl != tc.wl {
			t.Errorf("PseudoLocs #%d, got leaf tag=%v, want %v", tx, gl, tc.wl)
		}
		if locs := countSampleLocs(prof); !reflect.DeepEqual(locs, tc.locs) {
			t.Errorf("PseudoLocs #%d, got locs %v, want %v", tx, locs, tc.locs)
		}
	}
}
