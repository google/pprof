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
	"bytes"
	"fmt"
	"reflect"
	"strconv"
	"strings"
	"testing"
)

func TestLegacyProfileType(t *testing.T) {
	type testcase struct {
		sampleTypes []string
		typeSet     [][]string
		want        bool
		setName     string
	}

	heap := heapzSampleTypes
	cont := contentionzSampleTypes
	testcases := []testcase{
		// True cases
		{[]string{"allocations", "size"}, heap, true, "heapzSampleTypes"},
		{[]string{"objects", "space"}, heap, true, "heapzSampleTypes"},
		{[]string{"inuse_objects", "inuse_space"}, heap, true, "heapzSampleTypes"},
		{[]string{"alloc_objects", "alloc_space"}, heap, true, "heapzSampleTypes"},
		{[]string{"contentions", "delay"}, cont, true, "contentionzSampleTypes"},
		// False cases
		{[]string{"objects"}, heap, false, "heapzSampleTypes"},
		{[]string{"objects", "unknown"}, heap, false, "heapzSampleTypes"},
		{[]string{"contentions", "delay"}, heap, false, "heapzSampleTypes"},
		{[]string{"samples", "cpu"}, heap, false, "heapzSampleTypes"},
		{[]string{"samples", "cpu"}, cont, false, "contentionzSampleTypes"},
	}

	for _, tc := range testcases {
		p := profileOfType(tc.sampleTypes)
		if got := isProfileType(p, tc.typeSet); got != tc.want {
			t.Error("isProfileType({"+strings.Join(tc.sampleTypes, ",")+"},", tc.setName, "), got", got, "want", tc.want)
		}
	}
}

func TestCpuParse(t *testing.T) {
	// profileString is a legacy encoded profile, represnted by words separated by ":"
	// Each sample has the form value : N : stack1..stackN
	// EOF is represented as "0:1:0"
	profileString := "1:3:100:999:100:"                                      // sample with bogus 999 and duplicate leaf
	profileString += "1:5:200:999:200:501:502:"                              // sample with bogus 999 and duplicate leaf
	profileString += "1:12:300:999:300:601:602:603:604:605:606:607:608:609:" // sample with bogus 999 and duplicate leaf
	profileString += "0:1:0000"                                              // EOF -- must use 4 bytes for the final zero

	p, err := cpuProfile([]byte(profileString), 1, parseString)
	if err != nil {
		t.Fatal(err)
	}

	if err := checkTestSample(p, []uint64{100}); err != nil {
		t.Error(err)
	}
	if err := checkTestSample(p, []uint64{200, 500, 501}); err != nil {
		t.Error(err)
	}
	if err := checkTestSample(p, []uint64{300, 600, 601, 602, 603, 604, 605, 606, 607, 608}); err != nil {
		t.Error(err)
	}
}

func parseString(b []byte) (uint64, []byte) {
	slices := bytes.SplitN(b, []byte(":"), 2)
	var value, remainder []byte
	if len(slices) > 0 {
		value = slices[0]
	}
	if len(slices) > 1 {
		remainder = slices[1]
	}
	v, _ := strconv.ParseUint(string(value), 10, 64)
	return v, remainder
}

func checkTestSample(p *Profile, want []uint64) error {
	for _, s := range p.Sample {
		got := []uint64{}
		for _, l := range s.Location {
			got = append(got, l.Address)
		}
		if reflect.DeepEqual(got, want) {
			return nil
		}
	}
	return fmt.Errorf("Could not find sample : %v", want)
}

// profileOfType creates an empty profile with only sample types set,
// for testing purposes only.
func profileOfType(sampleTypes []string) *Profile {
	p := new(Profile)
	p.SampleType = make([]*ValueType, len(sampleTypes))
	for i, t := range sampleTypes {
		p.SampleType[i] = new(ValueType)
		p.SampleType[i].Type = t
	}
	return p
}
