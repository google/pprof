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

// Package flagset provides type StringList that can be used as a type for
// commandline flags for providing a slice of strings as input.
package flagset

import (
	"fmt"
)

// StringList is an alias for slice of strings.
type StringList []string

// String returns a formatted string containing all the strings in the slice.
func (sl *StringList) String() string {
	var cl []string
	for _, s := range *sl {
		cl = append(cl, s)
	}
	return fmt.Sprint(cl)
}

// Set adds the provided string v to the slice.
func (sl *StringList) Set(v string) error {
	w := v
	*sl = append(*sl, w)
	return nil
}
