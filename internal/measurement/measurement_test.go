// Copyright 2017 Google Inc. All Rights Reserved.
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

package measurement

import (
	"math"
	"testing"
)

func TestScale(t *testing.T) {
	for _, tc := range []struct {
		value            int64
		fromUnit, toUnit string
		wantValue        float64
		wantUnit         string
	}{
		{1, "s", "ms", 1000, "ms"},
		{1, "kb", "b", 1024, "B"},
		{1, "kbyte", "b", 1024, "B"},
		{1, "kilobyte", "b", 1024, "B"},
		{1, "mb", "kb", 1024, "kB"},
		{1, "gb", "mb", 1024, "MB"},
		{1024, "gb", "tb", 1, "TB"},
		{1024, "tb", "pb", 1, "PB"},
		{2048, "mb", "auto", 2, "GB"},
		{3.1536e7, "s", "auto", 8760, "hrs"},
		{-1, "s", "ms", -1000, "ms"},
		{1, "foo", "count", 1, ""},
		{1, "foo", "bar", 1, "bar"},
		{2000, "count", "count", 2000, ""},
		{2000, "count", "auto", 2000, ""},
		{2000, "count", "minimum", 2000, ""},
		{8e10, "nanogcu", "petagcus", 8e-14, "P*GCU"},
		{1.5e10, "microGCU", "teraGCU", 1.5e-8, "T*GCU"},
		{3e6, "milliGCU", "gigagcu", 3e-6, "G*GCU"},
		{1000, "kilogcu", "megagcu", 1, "M*GCU"},
		{2000, "GCU", "kiloGCU", 2, "k*GCU"},
		{7, "megaGCU", "gcu", 7e6, "GCU"},
		{5, "gigagcus", "milligcu", 5e12, "m*GCU"},
		{7, "teragcus", "microGCU", 7e18, "u*GCU"},
		{1, "petaGCU", "nanogcus", 1e24, "n*GCU"},
		{100, "NanoGCU", "auto", 100, "n*GCU"},
		{5000, "nanogcu", "auto", 5, "u*GCU"},
		{3000, "MicroGCU", "auto", 3, "m*GCU"},
		{4000, "MilliGCU", "auto", 4, "GCU"},
		{4000, "GCU", "auto", 4, "k*GCU"},
		{5000, "KiloGCU", "auto", 5, "M*GCU"},
		{6000, "MegaGCU", "auto", 6, "G*GCU"},
		{7000, "GigaGCU", "auto", 7, "T*GCU"},
		{8000, "TeraGCU", "auto", 8, "P*GCU"},
		{9000, "PetaGCU", "auto", 9000, "P*GCU"},
	} {
		if gotValue, gotUnit := Scale(tc.value, tc.fromUnit, tc.toUnit); !floatEqual(gotValue, tc.wantValue) || gotUnit != tc.wantUnit {
			t.Errorf("Scale(%d, %q, %q) = (%g, %q), want (%g, %q)",
				tc.value, tc.fromUnit, tc.toUnit, gotValue, gotUnit, tc.wantValue, tc.wantUnit)
		}
	}
}

func floatEqual(a, b float64) bool {
	diff := math.Abs(a - b)
	avg := (math.Abs(a) + math.Abs(b)) / 2
	return diff/avg < 0.0001
}
