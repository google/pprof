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

package driver

import (
	"testing"
)

func TestString(t *testing.T) {
	s1 := "test1"
	s2a := "test2a"
	s2b := "test2b"
	for _, tc := range []struct {
		sl   stringList
		want string
	}{
		{stringList([]*string{}), "[]"},
		{stringList([]*string{&s1}), "[test1]"},
		{stringList([]*string{&s2a, &s2b}), "[test2a test2b]"},
	} {
		got := tc.sl.String()
		if got != tc.want {
			t.Errorf("want %s, got %s", tc.want, got)
		}
	}
}

func TestSet(t *testing.T) {
	s1 := "test1"
	s2a := "test2a"
	s2b := "test2b"
	for _, tc := range []struct {
		in   stringList
		want []string
		add  string
	}{
		{stringList([]*string{}), []string{s1}, s1},
		{stringList([]*string{&s2a}), []string{s2a, s2b}, s2b},
	} {
		ok := tc.in.Set(tc.add)
		if ok != nil {
			t.Errorf("%v", ok)
		}
		if len(tc.in) != len(tc.want) {
			t.Errorf("want len %d, got len %d", len(tc.want), len(tc.in))
		}
		for i, s := range tc.in {
			if *s != tc.want[i] {
				t.Errorf("want %s, got %s", tc.want[i], *s)
			}
		}
	}
}
