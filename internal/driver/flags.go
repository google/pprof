//  Copyright 2018 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

package driver

import (
	"flag"
	"fmt"
	"strings"
)

// GoFlags implements the plugin.FlagSet interface.
type GoFlags struct {
	UsageMsgs []string
}

// Bool implements the plugin.FlagSet interface.
func (*GoFlags) Bool(o string, d bool, c string) *bool {
	return flag.Bool(o, d, c)
}

// Int implements the plugin.FlagSet interface.
func (*GoFlags) Int(o string, d int, c string) *int {
	return flag.Int(o, d, c)
}

// Float64 implements the plugin.FlagSet interface.
func (*GoFlags) Float64(o string, d float64, c string) *float64 {
	return flag.Float64(o, d, c)
}

// String implements the plugin.FlagSet interface.
func (*GoFlags) String(o, d, c string) *string {
	return flag.String(o, d, c)
}

type stringList []*string

func (sl *stringList) Set(v string) error {
	*sl = append(*sl, &v)
	return nil
}

func (sl *stringList) Get() interface{} {
	return sl
}

func (sl *stringList) String() string {
	var cl []string
	for _, s := range *sl {
		cl = append(cl, *s)
	}
	return fmt.Sprint(cl)
}

// StringList implements the plugin.FlagSet interface.
func (*GoFlags) StringList(o, d, c string) *[]*string {
	sl := &stringList{}
	if len(d) > 0 {
		sl.Set(d)
	}
	flag.Var(sl, o, c)
	return (*[]*string)(sl)
}

// ExtraUsage implements the plugin.FlagSet interface.
func (f *GoFlags) ExtraUsage() string {
	return strings.Join(f.UsageMsgs, "\n")
}

// AddExtraUsage implements the plugin.FlagSet interface.
func (f *GoFlags) AddExtraUsage(eu string) {
	f.UsageMsgs = append(f.UsageMsgs, eu)
}

// Parse implements the plugin.FlagSet interface.
func (*GoFlags) Parse(usage func()) []string {
	flag.Usage = usage
	flag.Parse()
	args := flag.Args()
	if len(args) == 0 {
		usage()
	}
	return args
}
