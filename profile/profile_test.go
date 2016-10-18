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
	"io/ioutil"
	"path/filepath"
	"reflect"
	"regexp"
	"testing"

	"github.com/google/pprof/internal/proftest"
)

func TestParse(t *testing.T) {
	const path = "testdata/"

	for _, source := range []string{
		"go.crc32.cpu",
		"go.godoc.thread",
		"gobench.cpu",
		"gobench.heap",
		"cppbench.cpu",
		"cppbench.heap",
		"cppbench.contention",
		"cppbench.growth",
		"cppbench.thread",
		"cppbench.thread.all",
		"cppbench.thread.none",
		"java.cpu",
		"java.heap",
		"java.contention",
	} {
		inbytes, err := ioutil.ReadFile(filepath.Join(path, source))
		if err != nil {
			t.Fatal(err)
		}
		p, err := Parse(bytes.NewBuffer(inbytes))
		if err != nil {
			t.Fatalf("%s: %s", source, err)
		}

		js := p.String()
		goldFilename := path + source + ".string"
		gold, err := ioutil.ReadFile(goldFilename)
		if err != nil {
			t.Fatalf("%s: %v", source, err)
		}

		if js != string(gold) {
			t.Errorf("diff %s %s", source, goldFilename)
			d, err := proftest.Diff(gold, []byte(js))
			if err != nil {
				t.Fatalf("%s: %v", source, err)
			}
			t.Error(source + "\n" + string(d) + "\n" + "new profile at:\n" + leaveTempfile([]byte(js)))
		}

		// Reencode and decode.
		bw := bytes.NewBuffer(nil)
		if err := p.Write(bw); err != nil {
			t.Fatalf("%s: %v", source, err)
		}
		if p, err = Parse(bw); err != nil {
			t.Fatalf("%s: %v", source, err)
		}
		js2 := p.String()
		if js2 != string(gold) {
			d, err := proftest.Diff(gold, []byte(js2))
			if err != nil {
				t.Fatalf("%s: %v", source, err)
			}
			t.Error(source + "\n" + string(d) + "\n" + "gold:\n" + goldFilename +
				"\nnew profile at:\n" + leaveTempfile([]byte(js)))
		}
	}
}

// leaveTempfile leaves |b| in a temporary file on disk and returns the
// temp filename. This is useful to recover a profile when the test
// fails.
func leaveTempfile(b []byte) string {
	f1, err := ioutil.TempFile("", "profile_test")
	if err != nil {
		panic(err)
	}
	if _, err := f1.Write(b); err != nil {
		panic(err)
	}
	return f1.Name()
}

const mainBinary = "/bin/main"

var cpuM = []*Mapping{
	{
		ID:              1,
		Start:           0x10000,
		Limit:           0x40000,
		File:            mainBinary,
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
	{
		ID:              2,
		Start:           0x1000,
		Limit:           0x4000,
		File:            "/lib/lib.so",
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
	{
		ID:              3,
		Start:           0x4000,
		Limit:           0x5000,
		File:            "/lib/lib2_c.so.6",
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
	{
		ID:              4,
		Start:           0x5000,
		Limit:           0x9000,
		File:            "/lib/lib.so_6 (deleted)",
		HasFunctions:    true,
		HasFilenames:    true,
		HasLineNumbers:  true,
		HasInlineFrames: true,
	},
}

var cpuF = []*Function{
	{ID: 1, Name: "main", SystemName: "main", Filename: "main.c"},
	{ID: 2, Name: "foo", SystemName: "foo", Filename: "foo.c"},
	{ID: 3, Name: "foo_caller", SystemName: "foo_caller", Filename: "foo.c"},
}

var cpuL = []*Location{
	{
		ID:      1000,
		Mapping: cpuM[1],
		Address: 0x1000,
		Line: []Line{
			{Function: cpuF[0], Line: 1},
		},
	},
	{
		ID:      2000,
		Mapping: cpuM[0],
		Address: 0x2000,
		Line: []Line{
			{Function: cpuF[1], Line: 2},
			{Function: cpuF[2], Line: 1},
		},
	},
	{
		ID:      3000,
		Mapping: cpuM[0],
		Address: 0x3000,
		Line: []Line{
			{Function: cpuF[1], Line: 2},
			{Function: cpuF[2], Line: 1},
		},
	},
	{
		ID:      3001,
		Mapping: cpuM[0],
		Address: 0x3001,
		Line: []Line{
			{Function: cpuF[2], Line: 2},
		},
	},
	{
		ID:      3002,
		Mapping: cpuM[0],
		Address: 0x3002,
		Line: []Line{
			{Function: cpuF[2], Line: 3},
		},
	},
}

var testProfile = &Profile{
	PeriodType:    &ValueType{Type: "cpu", Unit: "milliseconds"},
	Period:        1,
	DurationNanos: 10e9,
	SampleType: []*ValueType{
		{Type: "samples", Unit: "count"},
		{Type: "cpu", Unit: "milliseconds"},
	},
	Sample: []*Sample{
		{
			Location: []*Location{cpuL[0]},
			Value:    []int64{1000, 1000},
			Label: map[string][]string{
				"key1": []string{"tag1"},
				"key2": []string{"tag1"},
			},
		},
		{
			Location: []*Location{cpuL[1], cpuL[0]},
			Value:    []int64{100, 100},
			Label: map[string][]string{
				"key1": []string{"tag2"},
				"key3": []string{"tag2"},
			},
		},
		{
			Location: []*Location{cpuL[2], cpuL[0]},
			Value:    []int64{10, 10},
			Label: map[string][]string{
				"key1": []string{"tag3"},
				"key2": []string{"tag2"},
			},
		},
		{
			Location: []*Location{cpuL[3], cpuL[0]},
			Value:    []int64{10000, 10000},
			Label: map[string][]string{
				"key1": []string{"tag4"},
				"key2": []string{"tag1"},
			},
		},
		{
			Location: []*Location{cpuL[4], cpuL[0]},
			Value:    []int64{1, 1},
			Label: map[string][]string{
				"key1": []string{"tag4"},
				"key2": []string{"tag1"},
			},
		},
	},
	Location: cpuL,
	Function: cpuF,
	Mapping:  cpuM,
}

var aggTests = map[string]aggTest{
	"precise":         aggTest{true, true, true, true, 5},
	"fileline":        aggTest{false, true, true, true, 4},
	"inline_function": aggTest{false, true, false, true, 3},
	"function":        aggTest{false, true, false, false, 2},
}

type aggTest struct {
	precise, function, fileline, inlineFrame bool
	rows                                     int
}

const totalSamples = int64(11111)

func TestAggregation(t *testing.T) {
	prof := testProfile.Copy()
	for _, resolution := range []string{"precise", "fileline", "inline_function", "function"} {
		a := aggTests[resolution]
		if !a.precise {
			if err := prof.Aggregate(a.inlineFrame, a.function, a.fileline, a.fileline, false); err != nil {
				t.Error("aggregating to " + resolution + ":" + err.Error())
			}
		}
		if err := checkAggregation(prof, &a); err != nil {
			t.Error("failed aggregation to " + resolution + ": " + err.Error())
		}
	}
}

// checkAggregation verifies that the profile remained consistent
// with its aggregation.
func checkAggregation(prof *Profile, a *aggTest) error {
	// Check that the total number of samples for the rows was preserved.
	total := int64(0)

	samples := make(map[string]bool)
	for _, sample := range prof.Sample {
		tb := locationHash(sample)
		samples[tb] = true
		total += sample.Value[0]
	}

	if total != totalSamples {
		return fmt.Errorf("sample total %d, want %d", total, totalSamples)
	}

	// Check the number of unique sample locations
	if a.rows != len(samples) {
		return fmt.Errorf("number of samples %d, want %d", len(samples), a.rows)
	}

	// Check that all mappings have the right detail flags.
	for _, m := range prof.Mapping {
		if m.HasFunctions != a.function {
			return fmt.Errorf("unexpected mapping.HasFunctions %v, want %v", m.HasFunctions, a.function)
		}
		if m.HasFilenames != a.fileline {
			return fmt.Errorf("unexpected mapping.HasFilenames %v, want %v", m.HasFilenames, a.fileline)
		}
		if m.HasLineNumbers != a.fileline {
			return fmt.Errorf("unexpected mapping.HasLineNumbers %v, want %v", m.HasLineNumbers, a.fileline)
		}
		if m.HasInlineFrames != a.inlineFrame {
			return fmt.Errorf("unexpected mapping.HasInlineFrames %v, want %v", m.HasInlineFrames, a.inlineFrame)
		}
	}

	// Check that aggregation has removed finer resolution data.
	for _, l := range prof.Location {
		if !a.inlineFrame && len(l.Line) > 1 {
			return fmt.Errorf("found %d lines on location %d, want 1", len(l.Line), l.ID)
		}

		for _, ln := range l.Line {
			if !a.fileline && (ln.Function.Filename != "" || ln.Line != 0) {
				return fmt.Errorf("found line %s:%d on location %d, want :0",
					ln.Function.Filename, ln.Line, l.ID)
			}
			if !a.function && (ln.Function.Name != "") {
				return fmt.Errorf(`found file %s location %d, want ""`,
					ln.Function.Name, l.ID)
			}
		}
	}

	return nil
}

func TestParseMappingEntry(t *testing.T) {
	for _, test := range []*struct {
		entry string
		want  *Mapping
	}{
		{
			entry: "00400000-02e00000 r-xp 00000000 00:00 0",
			want: &Mapping{
				Start: 0x400000,
				Limit: 0x2e00000,
			},
		},
		{
			entry: "02e00000-02e8a000 r-xp 02a00000 00:00 15953927    /foo/bin",
			want: &Mapping{
				Start:  0x2e00000,
				Limit:  0x2e8a000,
				Offset: 0x2a00000,
				File:   "/foo/bin",
			},
		},
		{
			entry: "02e00000-02e8a000 r-xp 000000 00:00 15953927    [vdso]",
			want: &Mapping{
				Start: 0x2e00000,
				Limit: 0x2e8a000,
				File:  "[vdso]",
			},
		},
		{
			entry: "  02e00000-02e8a000: /foo/bin (@2a00000)",
			want: &Mapping{
				Start:  0x2e00000,
				Limit:  0x2e8a000,
				Offset: 0x2a00000,
				File:   "/foo/bin",
			},
		},
		{
			entry: "  02e00000-02e8a000: /foo/bin",
			want: &Mapping{
				Start: 0x2e00000,
				Limit: 0x2e8a000,
				File:  "/foo/bin",
			},
		},
		{
			entry: "  02e00000-02e8a000: [vdso]",
			want: &Mapping{
				Start: 0x2e00000,
				Limit: 0x2e8a000,
				File:  "[vdso]",
			},
		},
	} {
		got, err := parseMappingEntry(test.entry)
		if err != nil {
			t.Error(err)
		}
		if !reflect.DeepEqual(test.want, got) {
			t.Errorf("%s want=%v got=%v", test.entry, test.want, got)
		}
	}
}

// Test merge leaves the main binary in place.
func TestMergeMain(t *testing.T) {
	prof := testProfile.Copy()
	p1, err := Merge([]*Profile{prof})
	if err != nil {
		t.Fatalf("merge error: %v", err)
	}
	if cpuM[0].File != p1.Mapping[0].File {
		t.Errorf("want Mapping[0]=%s got %s", cpuM[0].File, p1.Mapping[0].File)
	}
}

func TestMerge(t *testing.T) {
	// Aggregate a profile with itself and once again with a factor of
	// -2. Should end up with an empty profile (all samples for a
	// location should add up to 0).

	prof := testProfile.Copy()
	p1, err := Merge([]*Profile{prof, prof})
	if err != nil {
		t.Errorf("merge error: %v", err)
	}
	prof.Scale(-2)
	prof, err = Merge([]*Profile{p1, prof})
	if err != nil {
		t.Errorf("merge error: %v", err)
	}

	// Use aggregation to merge locations at function granularity.
	if err := prof.Aggregate(false, true, false, false, false); err != nil {
		t.Errorf("aggregating after merge: %v", err)
	}

	samples := make(map[string]int64)
	for _, s := range prof.Sample {
		tb := locationHash(s)
		samples[tb] = samples[tb] + s.Value[0]
	}
	for s, v := range samples {
		if v != 0 {
			t.Errorf("nonzero value for sample %s: %d", s, v)
		}
	}
}

func TestMergeAll(t *testing.T) {
	// Aggregate 10 copies of the profile.
	profs := make([]*Profile, 10)
	for i := 0; i < 10; i++ {
		profs[i] = testProfile.Copy()
	}
	prof, err := Merge(profs)
	if err != nil {
		t.Errorf("merge error: %v", err)
	}
	samples := make(map[string]int64)
	for _, s := range prof.Sample {
		tb := locationHash(s)
		samples[tb] = samples[tb] + s.Value[0]
	}
	for _, s := range testProfile.Sample {
		tb := locationHash(s)
		if samples[tb] != s.Value[0]*10 {
			t.Errorf("merge got wrong value at %s : %d instead of %d", tb, samples[tb], s.Value[0]*10)
		}
	}
}

func TestFilter(t *testing.T) {
	// Perform several forms of filtering on the test profile.

	type filterTestcase struct {
		focus, ignore, hide, show *regexp.Regexp
		fm, im, hm, hnm           bool
	}

	for tx, tc := range []filterTestcase{
		{nil, nil, nil, nil, true, false, false, false},
		{regexp.MustCompile("notfound"), nil, nil, nil, false, false, false, false},
		{nil, regexp.MustCompile("foo.c"), nil, nil, true, true, false, false},
		{nil, nil, regexp.MustCompile("lib.so"), nil, true, false, true, false},
	} {
		prof := *testProfile.Copy()
		gf, gi, gh, gnh := prof.FilterSamplesByName(tc.focus, tc.ignore, tc.hide, tc.show)
		if gf != tc.fm {
			t.Errorf("Filter #%d, got fm=%v, want %v", tx, gf, tc.fm)
		}
		if gi != tc.im {
			t.Errorf("Filter #%d, got im=%v, want %v", tx, gi, tc.im)
		}
		if gh != tc.hm {
			t.Errorf("Filter #%d, got hm=%v, want %v", tx, gh, tc.hm)
		}
		if gnh != tc.hnm {
			t.Errorf("Filter #%d, got hnm=%v, want %v", tx, gnh, tc.hnm)
		}
	}
}

func TestTagFilter(t *testing.T) {
	// Perform several forms of tag filtering on the test profile.

	type filterTestcase struct {
		include, exclude *regexp.Regexp
		im, em           bool
		count            int
	}

	countTags := func(p *Profile) map[string]bool {
		tags := make(map[string]bool)

		for _, s := range p.Sample {
			for l := range s.Label {
				tags[l] = true
			}
			for l := range s.NumLabel {
				tags[l] = true
			}
		}
		return tags
	}

	for tx, tc := range []filterTestcase{
		{nil, nil, true, false, 3},
		{regexp.MustCompile("notfound"), nil, false, false, 0},
		{regexp.MustCompile("key1"), nil, true, false, 1},
		{nil, regexp.MustCompile("key[12]"), true, true, 1},
	} {
		prof := testProfile.Copy()
		gim, gem := prof.FilterTagsByName(tc.include, tc.exclude)
		if gim != tc.im {
			t.Errorf("Filter #%d, got include match=%v, want %v", tx, gim, tc.im)
		}
		if gem != tc.em {
			t.Errorf("Filter #%d, got exclude match=%v, want %v", tx, gem, tc.em)
		}
		if tags := countTags(prof); len(tags) != tc.count {
			t.Errorf("Filter #%d, got %d tags[%v], want %d", tx, len(tags), tags, tc.count)
		}
	}
}

// locationHash constructs a string to use as a hashkey for a sample, based on its locations
func locationHash(s *Sample) string {
	var tb string
	for _, l := range s.Location {
		for _, ln := range l.Line {
			tb = tb + fmt.Sprintf("%s:%d@%d ", ln.Function.Name, ln.Line, l.Address)
		}
	}
	return tb
}

func TestSetMain(t *testing.T) {
	testProfile.massageMappings()
	if testProfile.Mapping[0].File != mainBinary {
		t.Errorf("got %s for main", testProfile.Mapping[0].File)
	}
}

// Benchmarks

// benchmarkMerge measures the overhead of merging profiles read from files.
// They must be the same type of profiles.
func benchmarkMerge(b *testing.B, files []string) {
	const path = "testdata/"

	p := make([]*Profile, len(files))

	for i, source := range files {
		inBytes, err := ioutil.ReadFile(filepath.Join(path, source))
		if err != nil {
			b.Fatal(err)
		}
		if p[i], err = Parse(bytes.NewBuffer(inBytes)); err != nil {
			b.Fatalf("%s: %s", source, err)
		}
	}

	var prof *Profile
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		prof, _ = Merge(p)
	}
	b.StopTimer()

	before := 0
	for _, p := range p {
		p.preEncode()
		buff := marshal(p)
		before += len(buff)
	}
	prof.preEncode()
	buff := marshal(prof)
	after := len(buff)
	b.Logf("Profile size before merge = %v, After merge = %v", before, after)
}

// BenchmarkMergeCppCPUMedium measures the overhead of merging two medium CPU
// profiles of a C++ program (muppet).
func BenchmarkMergeCppCPUMedium(b *testing.B) {
	files := []string{
		"muppet.profilez.medium.1.pb.gz",
		"muppet.profilez.medium.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeCppHeapMedium measures the overhead of merging two medium Heap
// profiles of a C++ program (muppet).
func BenchmarkMergeCppHeapMedium(b *testing.B) {
	files := []string{
		"muppet.heapz.medium.1.pb.gz",
		"muppet.heapz.medium.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeCppContentionMedium measures the overhead of merging two medium
// contention profiles of a C++ program (muppet).
func BenchmarkMergeCppContentionMedium(b *testing.B) {
	files := []string{
		"muppet.contentionz.medium.1.pb.gz",
		"muppet.contentionz.medium.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaCPUMedium measures the overhead of merging two medium CPU
// profiles of a Java program (caribou).
func BenchmarkMergeJavaCPUMedium(b *testing.B) {
	files := []string{
		"caribou.profilez.medium.1.pb.gz",
		"caribou.profilez.medium.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaHeapMedium measures the overhead of merging two medium Heap
// profiles of a Java program (caribou).
func BenchmarkMergeJavaHeapMedium(b *testing.B) {
	files := []string{
		"caribou.heapz.medium.1.pb.gz",
		"caribou.heapz.medium.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaContentionMedium measures the overhead of merging two medium
// contention profiles of a Java program (caribou).
func BenchmarkMergeJavaContentionMedium(b *testing.B) {
	files := []string{
		"caribou.contentionz.medium.1.pb.gz",
		"caribou.contentionz.medium.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeCppCPULarge measures the overhead of merging two large CPU
// profiles of a C++ program (muppet).
func BenchmarkMergeCppCPULarge(b *testing.B) {
	files := []string{
		"muppet.profilez.large.1.pb.gz",
		"muppet.profilez.large.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeCppHeapLarge measures the overhead of merging two large Heap
// profiles of a C++ program (muppet).
func BenchmarkMergeCppHeapLarge(b *testing.B) {
	files := []string{
		"muppet.heapz.large.1.pb.gz",
		"muppet.heapz.large.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeCppContentionLarge measures the overhead of merging two large
// contention profiles of a C++ program (muppet).
func BenchmarkMergeCppContentionLarge(b *testing.B) {
	files := []string{
		"muppet.contentionz.large.1.pb.gz",
		"muppet.contentionz.large.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaCPULarge measures the overhead of merging two large CPU
// profiles of a Java program (caribou).
func BenchmarkMergeJavaCPULarge(b *testing.B) {
	files := []string{
		"caribou.profilez.large.1.pb.gz",
		"caribou.profilez.large.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaHeapLarge measures the overhead of merging two large Heap
// profiles of a Java program (caribou).
func BenchmarkMergeJavaHeapLarge(b *testing.B) {
	files := []string{
		"caribou.heapz.large.1.pb.gz",
		"caribou.heapz.large.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaContentionLarge measures the overhead of merging two large
// contention profiles of a Java program (caribou).
func BenchmarkMergeJavaContentionLarge(b *testing.B) {
	files := []string{
		"caribou.contentionz.large.1.pb.gz",
		"caribou.contentionz.large.2.pb.gz",
	}

	benchmarkMerge(b, files)
}

// BenchmarkMergeJavaCPUWorst measures the overhead of merging rollups worth 7 days
// for the worst case scenario. These rollups are generated by merging samples
// (10 seconds/min) from /profilez handler of caribou prod jobs. They are deduplicated
// so that Samples, Locations, Mappings, and Functions are unique.
func BenchmarkMergeJavaCPUWorst(b *testing.B) {
	files := []string{
		"caribou.profilez.1min.1.pb.gz",
		"caribou.profilez.1min.2.pb.gz",
		"caribou.profilez.1min.3.pb.gz",
		"caribou.profilez.1min.4.pb.gz",
		"caribou.profilez.1min.5.pb.gz",
		"caribou.profilez.1min.6.pb.gz",
		"caribou.profilez.1min.7.pb.gz",
		"caribou.profilez.1min.8.pb.gz",
		"caribou.profilez.1min.9.pb.gz",
		"caribou.profilez.1min.10.pb.gz",
		"caribou.profilez.1min.11.pb.gz",
		"caribou.profilez.1min.12.pb.gz",
		"caribou.profilez.1min.13.pb.gz",
		"caribou.profilez.1min.14.pb.gz",
		"caribou.profilez.1min.15.pb.gz",
		"caribou.profilez.1min.16.pb.gz",
		"caribou.profilez.1min.17.pb.gz",
		"caribou.profilez.1min.18.pb.gz",
		"caribou.profilez.10mins.1.pb.gz",
		"caribou.profilez.10mins.2.pb.gz",
		"caribou.profilez.10mins.3.pb.gz",
		"caribou.profilez.10mins.4.pb.gz",
		"caribou.profilez.10mins.5.pb.gz",
		"caribou.profilez.10mins.6.pb.gz",
		"caribou.profilez.10mins.7.pb.gz",
		"caribou.profilez.10mins.8.pb.gz",
		"caribou.profilez.10mins.9.pb.gz",
		"caribou.profilez.10mins.10.pb.gz",
		"caribou.profilez.1hour.1.pb.gz",
		"caribou.profilez.1hour.2.pb.gz",
		"caribou.profilez.1hour.3.pb.gz",
		"caribou.profilez.1hour.4.pb.gz",
		"caribou.profilez.1hour.5.pb.gz",
		"caribou.profilez.1hour.6.pb.gz",
		"caribou.profilez.4hours.1.pb.gz",
		"caribou.profilez.4hours.2.pb.gz",
		"caribou.profilez.4hours.3.pb.gz",
		"caribou.profilez.4hours.4.pb.gz",
		"caribou.profilez.12hours.1.pb.gz",
		"caribou.profilez.12hours.2.pb.gz",
		"caribou.profilez.1day.1.pb.gz",
		"caribou.profilez.1day.2.pb.gz",
		"caribou.profilez.1day.3.pb.gz",
		"caribou.profilez.1day.4.pb.gz",
		"caribou.profilez.1day.5.pb.gz",
	}

	benchmarkMerge(b, files)
}
