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
	"slices"
	"strconv"
	"strings"

	"github.com/google/pprof/internal/measurement"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

var tagFilterRangeRx = regexp.MustCompile("([+-]?[[:digit:]]+)([[:alpha:]]+)?")

// applyFocus filters samples based on the focus/ignore options
func applyFocus(prof *profile.Profile, numLabelUnits map[string]string, cfg config, ui plugin.UI) error {
	focus, err := compileRegexOption("focus", cfg.Focus, nil)
	ignore, err := compileRegexOption("ignore", cfg.Ignore, err)
	hide, err := compileRegexOption("hide", cfg.Hide, err)
	show, err := compileRegexOption("show", cfg.Show, err)
	showfrom, err := compileRegexOption("show_from", cfg.ShowFrom, err)
	tagfocus, err := compileTagFilter("tagfocus", cfg.TagFocus, numLabelUnits, ui, err)
	tagignore, err := compileTagFilter("tagignore", cfg.TagIgnore, numLabelUnits, ui, err)
	prunefrom, err := compileRegexOption("prune_from", cfg.PruneFrom, err)
	if err != nil {
		return err
	}

	fm, im, hm, hnm := prof.FilterSamplesByName(focus, ignore, hide, show)
	warnNoMatches(focus == nil || fm, "Focus", ui)
	warnNoMatches(ignore == nil || im, "Ignore", ui)
	warnNoMatches(hide == nil || hm, "Hide", ui)
	warnNoMatches(show == nil || hnm, "Show", ui)

	sfm := prof.ShowFrom(showfrom)
	warnNoMatches(showfrom == nil || sfm, "ShowFrom", ui)

	tfm, tim := prof.FilterSamplesByTag(tagfocus, tagignore)
	warnNoMatches(tagfocus == nil || tfm, "TagFocus", ui)
	warnNoMatches(tagignore == nil || tim, "TagIgnore", ui)

	tagshow, err := compileRegexOption("tagshow", cfg.TagShow, err)
	taghide, err := compileRegexOption("taghide", cfg.TagHide, err)
	tns, tnh := prof.FilterTagsByName(tagshow, taghide)
	warnNoMatches(tagshow == nil || tns, "TagShow", ui)
	warnNoMatches(taghide == nil || tnh, "TagHide", ui)

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

func compileTagFilter(name, value string, numLabelUnits map[string]string, ui plugin.UI, err error) (func(*profile.Sample) bool, error) {
	if value == "" || err != nil {
		return nil, err
	}

	expression := value
	tagValuePair := strings.SplitN(value, "=", 2)
	var wantKey string
	if len(tagValuePair) == 2 {
		wantKey = tagValuePair[0]
		value = tagValuePair[1]
	}

	numFilter, unit, rangeErr := parseTagFilterRange(value)
	if rangeErr != nil {
		ui.PrintErr(fmt.Sprintf("%s: %v in %q; interpreting as regexp", name, rangeErr, expression))
	}
	if numFilter != nil {
		ui.PrintErr(name, ":Interpreted '", value, "' as range, not regexp")
		if incompatible := incompatibleTagUnits(wantKey, unit, numLabelUnits); len(incompatible) != 0 {
			ui.PrintErr(fmt.Sprintf("%s: range %q has unit %q, incompatible with numeric tag units: %s", name, value, tagUnitName(unit), strings.Join(incompatible, ", ")))
		}
		labelFilter := func(vals []int64, unit string) bool {
			for _, val := range vals {
				if numFilter(val, unit) {
					return true
				}
			}
			return false
		}
		numLabelUnit := func(key string) string {
			return numLabelUnits[key]
		}
		if wantKey == "" {
			return func(s *profile.Sample) bool {
				for key, vals := range s.NumLabel {
					if labelFilter(vals, numLabelUnit(key)) {
						return true
					}
				}
				return false
			}, nil
		}
		return func(s *profile.Sample) bool {
			if vals, ok := s.NumLabel[wantKey]; ok {
				return labelFilter(vals, numLabelUnit(wantKey))
			}
			return false
		}, nil
	}

	var rfx []*regexp.Regexp
	for _, tagf := range strings.Split(value, ",") {
		fx, err := regexp.Compile(tagf)
		if err != nil {
			return nil, fmt.Errorf("parsing %s regexp: %v", name, err)
		}
		rfx = append(rfx, fx)
	}
	if wantKey == "" {
		return func(s *profile.Sample) bool {
		matchedrx:
			for _, rx := range rfx {
				for key, vals := range s.Label {
					for _, val := range vals {
						// TODO: Match against val, not key:val in future
						if rx.MatchString(key + ":" + val) {
							continue matchedrx
						}
					}
				}
				return false
			}
			return true
		}, nil
	}
	return func(s *profile.Sample) bool {
		if vals, ok := s.Label[wantKey]; ok {
			for _, rx := range rfx {
				if slices.ContainsFunc(vals, rx.MatchString) {
					return true
				}
			}
		}
		return false
	}, nil
}

// incompatibleTagUnits reports selected numeric tags only when none of them
// can match the filter's unit. Use the same conversion as the range predicate.
func incompatibleTagUnits(wantKey, unit string, numLabelUnits map[string]string) []string {
	var incompatible []string
	for key, labelUnit := range numLabelUnits {
		if wantKey != "" && key != wantKey {
			continue
		}
		if _, convertedUnit := measurement.Scale(0, labelUnit, unit); convertedUnit == unit {
			return nil
		}
		incompatible = append(incompatible, fmt.Sprintf("%q (%q)", key, tagUnitName(labelUnit)))
	}
	slices.Sort(incompatible)
	return incompatible
}

func tagUnitName(unit string) string {
	if unit == "" {
		return "count"
	}
	return unit
}

// parseTagFilterRange returns a predicate and its unit for a numeric range.
// If the range bounds have incompatible units, it returns an error explaining
// why the filter must be interpreted as a regexp instead. It can recognize
// strings of the form:
// "32kb" -- matches values == 32kb
// ":64kb" -- matches values <= 64kb
// "4mb:" -- matches values >= 4mb
// "12kb:64mb" -- matches values between 12kb and 64mb (both included).
func parseTagFilterRange(filter string) (func(int64, string) bool, string, error) {
	ranges := tagFilterRangeRx.FindAllStringSubmatch(filter, 2)
	if len(ranges) == 0 {
		return nil, "", nil // No ranges were identified
	}
	v, err := strconv.ParseInt(ranges[0][1], 10, 64)
	if err != nil {
		panic(fmt.Errorf("failed to parse int %s: %v", ranges[0][1], err))
	}
	scaledValue, unit := measurement.Scale(v, ranges[0][2], ranges[0][2])
	if len(ranges) == 1 {
		switch match := ranges[0][0]; filter {
		case match:
			return func(v int64, u string) bool {
				sv, su := measurement.Scale(v, u, unit)
				return su == unit && sv == scaledValue
			}, unit, nil
		case match + ":":
			return func(v int64, u string) bool {
				sv, su := measurement.Scale(v, u, unit)
				return su == unit && sv >= scaledValue
			}, unit, nil
		case ":" + match:
			return func(v int64, u string) bool {
				sv, su := measurement.Scale(v, u, unit)
				return su == unit && sv <= scaledValue
			}, unit, nil
		}
		return nil, "", nil
	}
	if filter != ranges[0][0]+":"+ranges[1][0] {
		return nil, "", nil
	}
	if v, err = strconv.ParseInt(ranges[1][1], 10, 64); err != nil {
		panic(fmt.Errorf("failed to parse int %s: %v", ranges[1][1], err))
	}
	scaledValue2, unit2 := measurement.Scale(v, ranges[1][2], unit)
	if unit != unit2 {
		return nil, "", fmt.Errorf("range bounds have incompatible units %q and %q", tagUnitName(unit), tagUnitName(unit2))
	}
	return func(v int64, u string) bool {
		sv, su := measurement.Scale(v, u, unit)
		return su == unit && sv >= scaledValue && sv <= scaledValue2
	}, unit, nil
}

func warnNoMatches(match bool, option string, ui plugin.UI) {
	if !match {
		ui.PrintErr(option + " expression matched no samples")
	}
}
