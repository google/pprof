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

// Package measurement export utility functions to manipulate/format performance profile sample values.
package measurement

import (
	"fmt"
	"math"
	"strings"
	"time"

	"github.com/google/pprof/profile"
)

// ScaleProfiles updates the units in a set of profiles to make them
// compatible. It scales the profiles to the smallest unit to preserve
// data.
func ScaleProfiles(profiles []*profile.Profile) error {
	if len(profiles) == 0 {
		return nil
	}
	periodTypes := make([]*profile.ValueType, 0, len(profiles))
	for _, p := range profiles {
		if p.PeriodType != nil {
			periodTypes = append(periodTypes, p.PeriodType)
		}
	}
	periodType, err := CommonValueType(periodTypes)
	if err != nil {
		return fmt.Errorf("period type: %v", err)
	}

	// Identify common sample types
	numSampleTypes := len(profiles[0].SampleType)
	for _, p := range profiles[1:] {
		if numSampleTypes != len(p.SampleType) {
			return fmt.Errorf("inconsistent samples type count: %d != %d", numSampleTypes, len(p.SampleType))
		}
	}
	sampleType := make([]*profile.ValueType, numSampleTypes)
	for i := 0; i < numSampleTypes; i++ {
		sampleTypes := make([]*profile.ValueType, len(profiles))
		for j, p := range profiles {
			sampleTypes[j] = p.SampleType[i]
		}
		sampleType[i], err = CommonValueType(sampleTypes)
		if err != nil {
			return fmt.Errorf("sample types: %v", err)
		}
	}

	for _, p := range profiles {
		if p.PeriodType != nil && periodType != nil {
			period, _ := Scale(p.Period, p.PeriodType.Unit, periodType.Unit)
			p.Period, p.PeriodType.Unit = int64(period), periodType.Unit
		}
		ratios := make([]float64, len(p.SampleType))
		for i, st := range p.SampleType {
			if sampleType[i] == nil {
				ratios[i] = 1
				continue
			}
			ratios[i], _ = Scale(1, st.Unit, sampleType[i].Unit)
			p.SampleType[i].Unit = sampleType[i].Unit
		}
		if err := p.ScaleN(ratios); err != nil {
			return fmt.Errorf("scale: %v", err)
		}
	}
	return nil
}

// CommonValueType returns the finest type from a set of compatible
// types.
func CommonValueType(ts []*profile.ValueType) (*profile.ValueType, error) {
	if len(ts) <= 1 {
		return nil, nil
	}
	minType := ts[0]
	for _, t := range ts[1:] {
		if !compatibleValueTypes(minType, t) {
			return nil, fmt.Errorf("incompatible types: %v %v", *minType, *t)
		}
		if ratio, _ := Scale(1, t.Unit, minType.Unit); ratio < 1 {
			minType = t
		}
	}
	rcopy := *minType
	return &rcopy, nil
}

func compatibleValueTypes(v1, v2 *profile.ValueType) bool {
	if v1 == nil || v2 == nil {
		return true // No grounds to disqualify.
	}
	// Remove trailing 's' to permit minor mismatches.
	if t1, t2 := strings.TrimSuffix(v1.Type, "s"), strings.TrimSuffix(v2.Type, "s"); t1 != t2 {
		return false
	}

	return v1.Unit == v2.Unit ||
		(isTimeUnit(v1.Unit) && isTimeUnit(v2.Unit)) ||
		(isMemoryUnit(v1.Unit) && isMemoryUnit(v2.Unit)) ||
		(isGCUUnit(v1.Unit) && isGCUUnit(v2.Unit))
}

// Scale a measurement from an unit to a different unit and returns
// the scaled value and the target unit. The returned target unit
// will be empty if uninteresting (could be skipped).
func Scale(value int64, fromUnit, toUnit string) (float64, string) {
	// Avoid infinite recursion on overflow.
	if value < 0 && -value > 0 {
		v, u := Scale(-value, fromUnit, toUnit)
		return -v, u
	}
	if m, u, ok := memoryLabel(value, fromUnit, toUnit); ok {
		return m, u
	}
	if t, u, ok := timeLabel(value, fromUnit, toUnit); ok {
		return t, u
	}
	if g, u, ok := gcuLabel(value, fromUnit, toUnit); ok {
		return g, u
	}
	// Skip non-interesting units.
	switch toUnit {
	case "count", "sample", "unit", "minimum", "auto":
		return float64(value), ""
	default:
		return float64(value), toUnit
	}
}

// Label returns the label used to describe a certain measurement.
func Label(value int64, unit string) string {
	return ScaledLabel(value, unit, "auto")
}

// ScaledLabel scales the passed-in measurement (if necessary) and
// returns the label used to describe a float measurement.
func ScaledLabel(value int64, fromUnit, toUnit string) string {
	v, u := Scale(value, fromUnit, toUnit)
	sv := strings.TrimSuffix(fmt.Sprintf("%.2f", v), ".00")
	if sv == "0" || sv == "-0" {
		return "0"
	}
	return sv + u
}

// Percentage computes the percentage of total of a value, and encodes
// it as a string. At least two digits of precision are printed.
func Percentage(value, total int64) string {
	var ratio float64
	if total != 0 {
		ratio = math.Abs(float64(value)/float64(total)) * 100
	}
	switch {
	case math.Abs(ratio) >= 99.95 && math.Abs(ratio) <= 100.05:
		return "  100%"
	case math.Abs(ratio) >= 1.0:
		return fmt.Sprintf("%5.2f%%", ratio)
	default:
		return fmt.Sprintf("%5.2g%%", ratio)
	}
}

// unit includes a list of names representing a specific unit and a factor
// which one can multiple a value in the specified unit by to get the value
// in terms of the base unit.
type unit struct {
	preferredName string
	names         []string
	factor        float64
}

var memoryUnits = []unit{
	{"B", []string{"b", "byte"}, 1},
	{"kB", []string{"kb", "kbyte", "kilobyte"}, 1024},
	{"MB", []string{"mb", "mbyte", "megabyte"}, 1024 * 1024},
	{"GB", []string{"gb", "gbyte", "gigabyte"}, 1024 * 1024 * 1024},
	{"TB", []string{"tb", "tbyte", "terabyte"}, 1024 * 1024 * 1024 * 1024},
	{"PB", []string{"pb", "pbyte", "petabyte"}, 1024 * 1024 * 1024 * 1024 * 1024},
}

// isMemoryUnit returns whether a name is recognized as a memory size
// unit.
func isMemoryUnit(unit string) bool {
	_, _, memoryUnit := unitFactor(strings.TrimSuffix(strings.ToLower(unit), "s"), memoryUnits)
	return memoryUnit
}

func memoryLabel(value int64, fromUnit, toUnit string) (v float64, u string, ok bool) {
	fromUnit = strings.TrimSuffix(strings.ToLower(fromUnit), "s")
	toUnit = strings.TrimSuffix(strings.ToLower(toUnit), "s")

	uf := func(unit string) (string, float64, bool) {
		return unitFactor(unit, memoryUnits)
	}

	as := func(value float64) (float64, string, bool) {
		return autoscale(value, memoryUnits)
	}

	return convertUnit(value, fromUnit, toUnit, unit{"B", []string{}, 1.0}, uf, as)
}

var timeUnits = []unit{
	{"ns", []string{"ns", "nanosecond"}, float64(time.Nanosecond)},
	{"μs", []string{"μs", "us", "microsecond"}, float64(time.Microsecond)},
	{"ms", []string{"ms", "millisecond"}, float64(time.Millisecond)},
	{"s", []string{"s", "sec", "second"}, float64(time.Second)},
	{"hrs", []string{"hour", "hr"}, float64(time.Hour)},
	{"days", []string{"day"}, 24 * float64(time.Hour)},
	{"wks", []string{"wk", "week"}, 7 * 24 * float64(time.Hour)},
	{"yrs", []string{"yr", "year"}, 365 * 24 * float64(time.Hour)},
}

// isTimeUnit returns whether a name is recognized as a time unit.
func isTimeUnit(unit string) bool {
	unit = strings.ToLower(unit)
	if len(unit) > 2 {
		unit = strings.TrimSuffix(unit, "s")
	}

	_, _, timeUnit := unitFactor(unit, timeUnits)
	return timeUnit || unit == "cycle"
}

func timeLabel(value int64, fromUnit, toUnit string) (v float64, u string, ok bool) {
	fromUnit = strings.ToLower(fromUnit)
	if len(fromUnit) > 2 {
		fromUnit = strings.TrimSuffix(fromUnit, "s")
	}

	toUnit = strings.ToLower(toUnit)
	if len(toUnit) > 2 {
		toUnit = strings.TrimSuffix(toUnit, "s")
	}

	if fromUnit == "cycle" {
		return float64(value), "", true
	}

	uf := func(unit string) (string, float64, bool) {
		return unitFactor(unit, timeUnits)
	}
	as := func(value float64) (float64, string, bool) {
		return autoscale(value, timeUnits)
	}
	return convertUnit(value, fromUnit, toUnit, unit{"s", []string{}, float64(time.Second)}, uf, as)
}

var caseInsensitiveGCUUnits = []unit{
	{"n·GCU", []string{"nanogcu"}, 1e-9},
	{"μ·GCU", []string{"microgcu"}, 1e-6},
	{"m·GCU", []string{"milligcu"}, 1e-3},
	{"GCU", []string{"gcu"}, 1},
	{"k·GCU", []string{"kilogcu"}, 1e3},
	{"M·GCU", []string{"megagcu"}, 1e6},
	{"G·GCU", []string{"gigagcu"}, 1e9},
	{"T·GCU", []string{"teragcu"}, 1e12},
	{"P·GCU", []string{"petagcu"}, 1e15},
}

var caseSensitiveGCUUnits = []unit{
	{"n·GCU", []string{"n·GCU", "n*GCU"}, 1e-9},
	{"μ·GCU", []string{"μ·GCU", "μ*GCU", "u·GCU", "u*GCU"}, 1e-6},
	{"m·GCU", []string{"m·GCU", "m*GCU"}, 1e-3},
	{"k·GCU", []string{"k·GCU", "k*GCU"}, 1e3},
	{"M·GCU", []string{"M·GCU", "M*GCU"}, 1e6},
	{"G·GCU", []string{"G·GCU", "G*GCU"}, 1e9},
	{"T·GCU", []string{"T·GCU", "T*GCU"}, 1e12},
	{"P·GCU", []string{"P·GCU", "P*GCU"}, 1e15},
}

// isGCUUnit returns whether a name is recognized as a GCU unit.
func isGCUUnit(unit string) bool {
	_, _, gcuUnit := gcuUnitFactor(unit)
	return gcuUnit
}

func gcuUnitFactor(unit string) (string, float64, bool) {
	unit = strings.TrimSuffix(strings.TrimSuffix(unit, "s"), "S")
	if u, f, ok := unitFactor(unit, caseSensitiveGCUUnits); ok {
		return u, f, true
	}
	unit = strings.ToLower(unit)
	if u, f, ok := unitFactor(unit, caseInsensitiveGCUUnits); ok {
		return u, f, true
	}
	return "", 0, false
}

func gcuLabel(value int64, fromUnit, toUnit string) (v float64, u string, ok bool) {
	as := func(value float64) (float64, string, bool) {
		// use caseInsentiveGCUUnits for autoscaling, since only this list of
		// GCU units includes the unprefixed gcu unit.
		return autoscale(value, caseInsensitiveGCUUnits)
	}
	return convertUnit(value, fromUnit, toUnit, unit{"s", []string{}, float64(time.Second)}, gcuUnitFactor, as)
}

// convertUnit converts a value from the fromUnit to the toUnit, autoscaling
// the value if the toUnit is "minimum" or "auto". If the fromUnit is not
// included in units, then a false boolean will be returned. If the toUnit
// is not in units, the value will be returned in terms of the default unit.
func convertUnit(value int64, fromUnit, toUnit string, defaultUnit unit, unitFactor func(string) (string, float64, bool), autoscale func(float64) (float64, string, bool)) (v float64, u string, ok bool) {
	_, fromUnitFactor, ok := unitFactor(fromUnit)
	if !ok {
		return 0, "", false
	}

	v = float64(value) * fromUnitFactor

	if toUnit == "minimum" || toUnit == "auto" {
		if v, u, ok := autoscale(v); ok {
			return v, u, true
		}
		return v / defaultUnit.factor, defaultUnit.preferredName, true
	}

	toUnit, toUnitFactor, ok := unitFactor(toUnit)
	if !ok {
		return v / defaultUnit.factor, defaultUnit.preferredName, true
	}
	return v / toUnitFactor, toUnit, true
}

// unitFactor returns the preferred version of the unit name for display, the
// factor by which one must multiply a value specified in terms of unit in
// order to get the value specified in terms of the base unit, and a boolean
// to indicated if a unit factor was identified for the specified unit within
// the slice units.
func unitFactor(unit string, units []unit) (string, float64, bool) {
	for _, u := range units {
		for _, n := range u.names {
			if unit == n {
				return u.preferredName, u.factor, true
			}
		}
	}
	return unit, 0, false
}

// autoscale takes in the value with units of base unit and returns
// that value scaled to a reasonable unit if a reasonable unit is
// found.
func autoscale(value float64, units []unit) (float64, string, bool) {
	var f float64
	var unit string
	for _, u := range units {
		if u.factor >= f && (value/u.factor) >= 1.0 {
			f = u.factor
			unit = u.preferredName
		}
	}
	if f == 0 {
		return 0, "", false
	}
	return value / f, unit, true
}
