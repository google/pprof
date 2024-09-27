package report

import (
	"reflect"
	"testing"
)

func TestShortNames(t *testing.T) {
	type testCase struct {
		name string
		in   string
		out  []string
	}
	test := func(name, in string, out ...string) testCase {
		return testCase{name, in, out}
	}

	for _, c := range []testCase{
		test("empty", "", ""),
		test("simple", "foo", "foo"),
		test("trailingsep", "foo.bar.", "foo.bar.", "bar."),
		test("cplusplus", "a::b::c", "a::b::c", "b::c", "c"),
		test("dotted", "a.b.c", "a.b.c", "b.c", "c"),
		test("mixed_separators", "a::b.c::d", "a::b.c::d", "b.c::d", "c::d", "d"),
		test("call_operator", "foo::operator()", "foo::operator()", "operator()"),
	} {
		t.Run(c.name, func(t *testing.T) {
			got := shortNameList(c.in)
			if !reflect.DeepEqual(c.out, got) {
				t.Errorf("shortNameList(%q) = %#v, expecting %#v", c.in, got, c.out)
			}
		})
	}
}

func TestFileNameSuffixes(t *testing.T) {
	type testCase struct {
		name string
		in   string
		out  []string
	}
	test := func(name, in string, out ...string) testCase {
		return testCase{name, in, out}
	}

	for _, c := range []testCase{
		test("empty", "", ""),
		test("simple", "foo", "foo"),
		test("manypaths", "a/b/c", "a/b/c", "b/c", "c"),
		test("leading", "/a/b", "/a/b", "a/b", "b"),
		test("trailing", "a/b", "a/b", "b"),
	} {
		t.Run(c.name, func(t *testing.T) {
			got := fileNameSuffixes(c.in)
			if !reflect.DeepEqual(c.out, got) {
				t.Errorf("fileNameSuffixes(%q) = %#v, expecting %#v", c.in, got, c.out)
			}
		})
	}
}
