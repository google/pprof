package report

import (
	"testing"

	"github.com/google/pprof/profile"
)

func TestSynthAddresses(t *testing.T) {
	s := newSynthCode(nil)
	l1 := &profile.Location{}
	addr1 := s.address(l1)
	if s.address(l1) != addr1 {
		t.Errorf("different calls with same location returned different addresses")
	}

	l2 := &profile.Location{}
	addr2 := s.address(l2)
	if addr2 == addr1 {
		t.Errorf("same address assigned to different locations")
	}

}

func TestSynthAvoidsMapping(t *testing.T) {
	mappings := []*profile.Mapping{
		{Start: 100, Limit: 200},
		{Start: 300, Limit: 400},
	}
	s := newSynthCode(mappings)
	loc := &profile.Location{}
	addr := s.address(loc)
	if addr >= 100 && addr < 200 || addr >= 300 && addr < 400 {
		t.Errorf("synthetic location %d overlaps mapping %v", addr, mappings)
	}
}
