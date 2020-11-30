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

// Implements methods to add pseudo locations to samples.

import "fmt"

func findUniqueLoc(p *Profile) uint64 {
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

func addStrFun(p *Profile, tag string, sl []string) *Function {
	fun := new(Function)
	fun.ID = findUniqueFun(p)
	fun.Name = fmt.Sprint(tag, sl)
	p.Function = append(p.Function, fun)
	return fun
}

func addNumFun(p *Profile, tag string, nums []int64, units []string) *Function {
	fun := new(Function)
	fun.ID = findUniqueFun(p)
	for i, n := range nums {
		fun.Name = fun.Name + fmt.Sprintf(" %d %s", n, units[i])
	}
	p.Function = append(p.Function, fun)
	return fun
}

func addStringLoc(p *Profile, tag string, sl []string) *Location {
	loc := new(Location)
	loc.ID = findUniqueLoc(p)
	var line Line
	line.Function = addStrFun(p, tag, sl)
	loc.Line = append(loc.Line, line)
	loc.IsFolded = false
	p.Location = append(p.Location, loc)
	return loc
}

func addNumLoc(p *Profile, tag string, nl []int64, nu []string) *Location {
	loc := new(Location)
	loc.ID = findUniqueLoc(p)
	var line Line
	line.Function = addNumFun(p, tag, nl, nu)
	loc.Line = append(loc.Line, line)
	loc.IsFolded = false
	p.Location = append(p.Location, loc)
	return loc
}

// AddPseudoLocs adds root and leaf locations for tags in a profile as
// requested in the provided options.
func (p *Profile) AddPseudoLocs(rootTags, leafTags []*string) (bool, bool) {
	addedRootLoc := false
	addedLeafLoc := false
	for _, s := range p.Sample {
		rootLocs := make([]*Location, 0)
		for _, tag := range rootTags {
			l1, ok1 := s.Label[*tag]
			l2, ok2 := s.NumLabel[*tag]
			l3, _ := s.NumUnit[*tag]
			if ok1 || ok2 {
				addedRootLoc = true
				var loc *Location
				if ok1 {
					loc = addStringLoc(p, *tag, l1)
				} else {
					loc = addNumLoc(p, *tag, l2, l3)
				}
				rootLocs = append(rootLocs, loc)
			}
		}
		s.Location = append(rootLocs, s.Location...)
		for _, tag := range leafTags {
			l1, ok1 := s.Label[*tag]
			l2, ok2 := s.NumLabel[*tag]
			l3, _ := s.NumUnit[*tag]
			if ok1 || ok2 {
				addedLeafLoc = true
				var loc *Location
				if ok1 {
					loc = addStringLoc(p, *tag, l1)
				} else {
					loc = addNumLoc(p, *tag, l2, l3)
				}
				s.Location = append(s.Location, loc)
			}
		}
	}
	return addedRootLoc, addedLeafLoc
}
