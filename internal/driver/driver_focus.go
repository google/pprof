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

package driver

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/google/pprof/internal/measurement"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

var tagFilterRangeRx = regexp.MustCompile("([[:digit:]]+)([[:alpha:]]+)")

// applyFocus filters samples based on the focus/ignore options
func applyFocus(prof *profile.Profile, v variables, ui plugin.UI) error {
	focus, err := compileRegexOption("focus", v["focus"].stringValue(), nil)
	ignore, err := compileRegexOption("ignore", v["ignore"].stringValue(), err)
	hide, err := compileRegexOption("hide", v["hide"].stringValue(), err)
	show, err := compileRegexOption("show", v["show"].stringValue(), err)
	tagfocus, err := compileTagFilter("tagfocus", v["tagfocus"].repeatableStringValue(), ui, err)
	tagignore, err := compileTagFilter("tagignore", v["tagignore"].repeatableStringValue(), ui, err)
	prunefrom, err := compileRegexOption("prune_from", v["prune_from"].stringValue(), err)
	if err != nil {
		return err
	}

	fm, im, hm, hnm := prof.FilterSamplesByName(focus, ignore, hide, show)
	warnNoMatches(focus == nil || fm, "Focus", ui)
	warnNoMatches(ignore == nil || im, "Ignore", ui)
	warnNoMatches(hide == nil || hm, "Hide", ui)
	warnNoMatches(show == nil || hnm, "Show", ui)

	tfm, tim := prof.FilterSamplesByTag(tagfocus, tagignore)
	warnNoMatches(tagfocus == nil || tfm, "TagFocus", ui)
	warnNoMatches(tagignore == nil || tim, "TagIgnore", ui)

	tagshow, err := compileRegexOption("tagshow", v["tagshow"].stringValue(), err)
	taghide, err := compileRegexOption("taghide", v["taghide"].stringValue(), err)
	tns, tnh := prof.FilterTagsByName(tagshow, taghide)
	warnNoMatches(tagshow == nil || tns, "TagShow", ui)
	warnNoMatches(tagignore == nil || tnh, "TagHide", ui)

	if prunefrom != nil {
		prof.PruneFrom(prunefrom)
	}
	return err
}

func compileRegexOption(name, value string, err error) (*regexp.Regexp, error) {
	if value == "" || err != nil {
		return nil, err
	}
	rx, err := regexp.Compile(value)
	if err != nil {
		return nil, fmt.Errorf("parsing %s regexp: %v", name, err)
	}
	return rx, nil
}

func compileTagFilter(name string, values []string, ui plugin.UI, err error) (func(*profile.Sample) bool, error) {
	if len(values) == 0 {
		return nil, err
	}

	numFilters := make(map[string]func(int64, string) bool)
	labelFilters := make(map[string][]*regexp.Regexp)

	for _, tagValue := range values {
		if tagValue == "" {
			continue
		}
		valuePair := strings.SplitN(tagValue, "=", 2)
		var key, value string
		switch len(valuePair) {
		case 1:
			key = ""
			value = valuePair[0]
		case 2:
			key = valuePair[0]
			value = valuePair[1]
		}

		if numFilter := parseTagFilterRange(value); numFilter != nil {
			numFilters[key] = numFilter
		} else {
			var rfx []*regexp.Regexp
			for _, tagf := range strings.Split(value, ",") {
				fx, err := regexp.Compile(tagf)
				if err != nil {
					return nil, fmt.Errorf("parsing %s regexp: %v", name, err)
				}
				rfx = append(rfx, fx)
			}
			labelFilters[key] = rfx
		}
	}
	if len(numFilters) > 0 || len(labelFilters) > 0 {
		return func(s *profile.Sample) bool {
			// check if key-specific numeric filter matches
			for key, filter := range numFilters {
				if key != "" {
					if vals, ok := s.NumLabel[key]; ok {
						for _, val := range vals {
							if filter(val, key) {
								return true
							}
						}
					}
				}
			}

			// check if general numeric filter matches
			if filter, ok := numFilters[""]; ok {
				for key, vals := range s.NumLabel {
					for _, val := range vals {
						if filter(val, key) {
							return true
						}
					}
				}
			}

			// check if key-specific label filter matches
			for key, rfx := range labelFilters {
				if key != "" {
					if vals, ok := s.Label[key]; ok {
						matched := true
					matchedrxkey:
						for _, rx := range rfx {
							for _, val := range vals {
								if rx.MatchString(val) {
									continue matchedrxkey
								}
							}
							matched = false
							break matchedrxkey
						}
						if matched {
							return true
						}
					}
				}
			}

			// check if general label filter matches
			if rfx, ok := labelFilters[""]; ok {
			matchedrx:
				for _, rx := range rfx {
					for key, vals := range s.Label {
						for _, val := range vals {
							if rx.MatchString(key + ":" + val) {
								continue matchedrx
							}
						}
					}
					return false
				}
				return true
			}

			return false
		}, nil
	}

	return nil, err
}

// parseTagFilterRange returns a function to checks if a value is
// contained on the range described by a string. It can recognize
// strings of the form:
// "32kb" -- matches values == 32kb
// ":64kb" -- matches values <= 64kb
// "4mb:" -- matches values >= 4mb
// "12kb:64mb" -- matches values between 12kb and 64mb (both included).
func parseTagFilterRange(filter string) func(int64, string) bool {
	ranges := tagFilterRangeRx.FindAllStringSubmatch(filter, 2)
	if len(ranges) == 0 {
		return nil // No ranges were identified
	}
	v, err := strconv.ParseInt(ranges[0][1], 10, 64)
	if err != nil {
		panic(fmt.Errorf("Failed to parse int %s: %v", ranges[0][1], err))
	}
	scaledValue, unit := measurement.Scale(v, ranges[0][2], ranges[0][2])
	if len(ranges) == 1 {
		switch match := ranges[0][0]; filter {
		case match:
			return func(v int64, u string) bool {
				sv, su := measurement.Scale(v, u, unit)
				return su == unit && sv == scaledValue
			}
		case match + ":":
			return func(v int64, u string) bool {
				sv, su := measurement.Scale(v, u, unit)
				return su == unit && sv >= scaledValue
			}
		case ":" + match:
			return func(v int64, u string) bool {
				sv, su := measurement.Scale(v, u, unit)
				return su == unit && sv <= scaledValue
			}
		}
		return nil
	}
	if filter != ranges[0][0]+":"+ranges[1][0] {
		return nil
	}
	if v, err = strconv.ParseInt(ranges[1][1], 10, 64); err != nil {
		panic(fmt.Errorf("Failed to parse int %s: %v", ranges[1][1], err))
	}
	scaledValue2, unit2 := measurement.Scale(v, ranges[1][2], unit)
	if unit != unit2 {
		return nil
	}
	return func(v int64, u string) bool {
		sv, su := measurement.Scale(v, u, unit)
		return su == unit && sv >= scaledValue && sv <= scaledValue2
	}
}

func warnNoMatches(match bool, option string, ui plugin.UI) {
	if !match {
		ui.PrintErr(option + " expression matched no samples")
	}
}
