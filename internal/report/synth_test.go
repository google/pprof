package report

import (
	"reflect"
	"testing"

	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

func TestSynthAddresses(t *testing.T) {
	s := &synthCode{}
	l1 := &profile.Location{}
	addr1 := s.address(l1)
	if s.address(l1) != addr1 {
		t.Errorf("different calls with same location returned different addresses")
	}
	if !s.contains(addr1) {
		t.Errorf("does not contain known synthetic address")
	}
	if s.contains(addr1 + 1) {
		t.Errorf("contains unknown synthetic address")
	}

	l2 := &profile.Location{}
	addr2 := s.address(l2)
	if addr2 == addr1 {
		t.Errorf("same address assigned to different locations")
	}

}

func TestSynthAvoidsMapping(t *testing.T) {
	s := &synthCode{mappings: []*profile.Mapping{
		{Start: 100, Limit: 200},
		{Start: 300, Limit: 400},
	}}
	loc := &profile.Location{}
	addr := s.address(loc)
	if addr >= 100 && addr < 200 || addr >= 300 && addr < 400 {
		t.Errorf("synthetic location %d overlaps mapping %v", addr, s.mappings)
	}
}

func TestSynthFrames(t *testing.T) {
	s := &synthCode{}
	loc := &profile.Location{
		Line: []profile.Line{
			{
				Function: &profile.Function{Name: "foo", Filename: "foo.cc"},
				Line:     100,
			},
			{
				Function: &profile.Function{Name: "bar", Filename: "bar.cc"},
				Line:     200,
			},
		},
	}
	frames := s.frames(s.address(loc))
	expect := []plugin.Frame{
		{Func: "foo", File: "foo.cc", Line: 100},
		{Func: "bar", File: "bar.cc", Line: 200},
	}
	if !reflect.DeepEqual(expect, frames) {
		t.Errorf("unexpected frames: %+v", frames)
	}
}
