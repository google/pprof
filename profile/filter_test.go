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
