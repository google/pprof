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

// Implements methods to add tags as locations to samples.

import "fmt"

// AddRootLeafTagsAsLocations adds root and leaf locations for tags in a
// profile as requested in the provided options.
//
// The arguments can be empty strings in which case no action would be taken.
// The two boolean return values indicate whether root and leaf locations were
// added to any of the samples respectively.
func (p *Profile) AddRootLeafTagsAsLocations(rootTag, leafTag string) (bool, bool) {
	addedRootLoc := false
	addedLeafLoc := false
	for _, s := range p.Sample {
		rootLoc, added := addTagAsLocation(p, s, rootTag)
		if added {
			addedRootLoc = true
			s.Location = append(s.Location, rootLoc)
		}
		leafLoc, added := addTagAsLocation(p, s, leafTag)
		if added {
			addedLeafLoc = true
			s.Location = append([]*Location{leafLoc}, s.Location...)
		}
	}
	return addedRootLoc, addedLeafLoc
}

func addTagAsLocation(p *Profile, s *Sample, tag string) (*Location, bool) {
	if tag == "" {
		return nil, false
	}
	if l1, ok1 := s.Label[tag]; ok1 {
		return addStringLocation(p, tag, l1), true
	}
	if l2, ok2 := s.NumLabel[tag]; ok2 {
		l3 := s.NumUnit[tag]
		return addNumLocation(p, tag, l2, l3), true
	}
	return nil, false
}

func addStringLocation(p *Profile, tag string, sl []string) *Location {
	loc := new(Location)
	loc.ID = findUniqueLocation(p)
	var line Line
	line.Function = addStrFun(p, tag, sl)
	loc.Line = append(loc.Line, line)
	loc.IsFolded = false
	p.Location = append(p.Location, loc)
	return loc
}

func addNumLocation(p *Profile, tag string, nl []int64, nu []string) *Location {
	loc := new(Location)
	loc.ID = findUniqueLocation(p)
	var line Line
	line.Function = addNumFun(p, tag, nl, nu)
	loc.Line = append(loc.Line, line)
	loc.IsFolded = false
	p.Location = append(p.Location, loc)
	return loc
}

func findUniqueLocation(p *Profile) uint64 {
	ids := make(map[uint64]bool)
	for _, loc := range p.Location {
		ids[loc.ID] = true
	}
	for id := uint64(1); ; id++ {
		if ok := ids[id]; !ok {
			return id
		}
	}
}

func addStrFun(p *Profile, tag string, sl []string) *Function {
	fun := new(Function)
	fun.ID = findUniqueFun(p)
	fun.Name = fmt.Sprint(tag, ":", sl)
	p.Function = append(p.Function, fun)
	return fun
}

func addNumFun(p *Profile, tag string, nums []int64, units []string) *Function {
	fun := new(Function)
	fun.ID = findUniqueFun(p)
	for i, n := range nums {
		fun.Name = fun.Name + fmt.Sprintf("%s:%d%s", tag, n, units[i])
	}
	p.Function = append(p.Function, fun)
	return fun
}

func findUniqueFun(p *Profile) uint64 {
	ids := make(map[uint64]bool)
	for _, fun := range p.Function {
		ids[fun.ID] = true
	}
	for id := uint64(1); ; id++ {
		if ok := ids[id]; !ok {
			return id
		}
	}
}
