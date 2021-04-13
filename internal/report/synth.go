package report

import (
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

// synthAsm is the special value for instructions added by syntheticAddress.
const synthAsm = ""

// synthCode holds synthesized code that we generate we find addresses
// in a profile that do not correspond to any known mappings.
type synthCode struct {
	mappings mappingList
	next     uint64                       // Counter used to generate synthesized addresses.
	loc      map[uint64]*profile.Location // Location for each synthesized address
	addr     map[*profile.Location]uint64 // Synthesized address assigned to a location
}

// address returns the synthetic address for loc, creating one if needed.
func (s *synthCode) address(loc *profile.Location) uint64 {
	if addr, ok := s.addr[loc]; ok {
		return addr
	}

	if s.loc == nil {
		s.loc = map[uint64]*profile.Location{}
		s.addr = map[*profile.Location]uint64{}

		// Initialize counter for assigning synthetic addresses.
		// For now, assume that there are available addresses past the last mapping.
		if len(s.mappings) > 0 {
			s.next = s.mappings[len(s.mappings)-1].Limit
		} else {
			s.next = 1
		}
	}

	addr := s.next
	s.next++
	s.loc[addr] = loc
	s.addr[loc] = addr
	return addr
}

// contains returns true if addr is a synthesized address.
func (s *synthCode) contains(addr uint64) bool {
	_, ok := s.loc[addr]
	return ok
}

// frames returns the []plugin.Frame list for the specified synthesized address.
// The result will typically have length 1, but may be longer if address corresponds
// to inlined calls.
func (s *synthCode) frames(addr uint64) []plugin.Frame {
	loc := s.loc[addr]
	stack := make([]plugin.Frame, 0, len(loc.Line))
	for _, line := range loc.Line {
		fn := line.Function
		if fn == nil {
			continue
		}
		stack = append(stack, plugin.Frame{
			Func: fn.Name,
			File: fn.Filename,
			Line: int(line.Line),
		})
	}
	return stack
}
