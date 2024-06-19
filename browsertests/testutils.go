package browsertests

import (
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"regexp"
	"runtime"
	"testing"
	"time"

	"github.com/google/pprof/driver"
	"github.com/google/pprof/profile"
)

func makeTestServer(t testing.TB, prof *profile.Profile) *httptest.Server {
	if runtime.GOOS == "nacl" || runtime.GOOS == "js" {
		t.Skip("test assumes tcp available")
	}

	// Custom http server creator
	var server *httptest.Server
	serverCreated := make(chan bool)
	creator := func(a *driver.HTTPServerArgs) error {
		server = httptest.NewServer(http.HandlerFunc(
			func(w http.ResponseWriter, r *http.Request) {
				if h := a.Handlers[r.URL.Path]; h != nil {
					h.ServeHTTP(w, r)
				}
			}))
		serverCreated <- true
		return nil
	}

	// Start server and wait for it to be initialized
	go func() {
		err := driver.PProf(&driver.Options{
			Obj:        fakeObjTool{},
			UI:         testUI{t},
			Fetch:      testFetcher{prof},
			HTTPServer: creator,
			Flagset: testFlags{
				"http":       "unused:1234",
				"no_browser": true,
			},
		})
		if err != nil {
			panic(err)
		}
	}()
	<-serverCreated

	// Close the server when the test is done.
	t.Cleanup(server.Close)

	return server
}

// Fake test implementations of types needed by pprof driver.

const addrBase = 0x1000
const fakeSource = "testdata/file1000.src"

type fakeObj struct{}

func (f fakeObj) Close() error                        { return nil }
func (f fakeObj) Name() string                        { return "testbin" }
func (f fakeObj) ObjAddr(addr uint64) (uint64, error) { return addr, nil }
func (f fakeObj) BuildID() string                     { return "" }
func (f fakeObj) SourceLine(addr uint64) ([]driver.Frame, error) {
	return nil, fmt.Errorf("SourceLine unimplemented")
}
func (f fakeObj) Symbols(r *regexp.Regexp, addr uint64) ([]*driver.Sym, error) {
	return []*driver.Sym{
		{
			Name: []string{"F1"}, File: fakeSource,
			Start: addrBase, End: addrBase + 10,
		},
		{
			Name: []string{"F2"}, File: fakeSource,
			Start: addrBase + 10, End: addrBase + 20,
		},
		{
			Name: []string{"F3"}, File: fakeSource,
			Start: addrBase + 20, End: addrBase + 30,
		},
	}, nil
}

type fakeObjTool struct{}

func (obj fakeObjTool) Open(file string, start, limit, offset uint64, relocationSymbol string) (driver.ObjFile, error) {
	return fakeObj{}, nil
}

func (obj fakeObjTool) Disasm(file string, start, end uint64, intelSyntax bool) ([]driver.Inst, error) {
	return []driver.Inst{
		{Addr: addrBase + 10, Text: "f1:asm", Function: "F1", Line: 3},
		{Addr: addrBase + 20, Text: "f2:asm", Function: "F2", Line: 11},
		{Addr: addrBase + 30, Text: "d3:asm", Function: "F3", Line: 22},
	}, nil
}

func makeFakeProfile() *profile.Profile {
	// Three functions: F1, F2, F3 with three lines, 11, 22, 33.
	funcs := []*profile.Function{
		{ID: 1, Name: "F1", Filename: fakeSource, StartLine: 3},
		{ID: 2, Name: "F2", Filename: fakeSource, StartLine: 5},
		{ID: 3, Name: "F3", Filename: fakeSource, StartLine: 7},
	}
	lines := []profile.Line{
		{Function: funcs[0], Line: 11},
		{Function: funcs[1], Line: 22},
		{Function: funcs[2], Line: 33},
	}
	mapping := []*profile.Mapping{
		{
			ID:             1,
			Start:          addrBase,
			Limit:          addrBase + 100,
			Offset:         0,
			File:           "testbin",
			HasFunctions:   true,
			HasFilenames:   true,
			HasLineNumbers: true,
		},
	}

	// Three interesting addresses: base+{10,20,30}
	locs := []*profile.Location{
		{ID: 1, Address: addrBase + 10, Line: lines[0:1], Mapping: mapping[0]},
		{ID: 2, Address: addrBase + 20, Line: lines[1:2], Mapping: mapping[0]},
		{ID: 3, Address: addrBase + 30, Line: lines[2:3], Mapping: mapping[0]},
	}

	// Two stack traces.
	return &profile.Profile{
		PeriodType:    &profile.ValueType{Type: "cpu", Unit: "milliseconds"},
		Period:        1,
		DurationNanos: 10e9,
		SampleType: []*profile.ValueType{
			{Type: "cpu", Unit: "milliseconds"},
		},
		Sample: []*profile.Sample{
			{
				Location: []*profile.Location{locs[2], locs[1], locs[0]},
				Value:    []int64{100},
			},
			{
				Location: []*profile.Location{locs[1], locs[0]},
				Value:    []int64{200},
			},
		},
		Location: locs,
		Function: funcs,
		Mapping:  mapping,
	}
}

type testFlags map[string]any

func (flags testFlags) Bool(name string, def bool, usage string) *bool {
	return getFlag(flags, name, def)
}
func (flags testFlags) Int(name string, def int, usage string) *int {
	return getFlag(flags, name, def)
}
func (flags testFlags) Float64(name string, def float64, usage string) *float64 {
	return getFlag(flags, name, def)
}
func (flags testFlags) String(name string, def string, usage string) *string {
	return getFlag(flags, name, def)
}
func (flags testFlags) StringList(name string, def string, usage string) *[]*string {
	return getFlag(flags, name, []*string{}) // Not supported, so return an empty list.
}
func (flags testFlags) ExtraUsage() string          { return "" }
func (flags testFlags) AddExtraUsage(eu string)     {}
func (flags testFlags) Parse(usage func()) []string { return []string{"test", "bin"} }

var _ driver.FlagSet = testFlags{}

func getFlag[T any](flags testFlags, name string, def T) *T {
	result := &def
	if v, ok := flags[name]; ok {
		*result = v.(T)
	}
	return result
}

type testUI struct {
	T testing.TB
}

func (ui testUI) ReadLine(_ string) (string, error)     { return "", io.EOF }
func (ui testUI) IsTerminal() bool                      { return false }
func (ui testUI) WantBrowser() bool                     { return false }
func (ui testUI) SetAutoComplete(_ func(string) string) {}
func (ui testUI) Print(args ...interface{})             {} // discard
func (ui testUI) PrintErr(args ...interface{}) {
	ui.T.Error("unexpected error: " + fmt.Sprint(args...))
}

var _ driver.UI = testUI{}

type testFetcher struct {
	profile *profile.Profile
}

func (f testFetcher) Fetch(source string, duration, timeout time.Duration) (*profile.Profile, string, error) {
	// http://pproftest.local prevents file from being saved.
	return f.profile, "http://pproftest.local", nil
}

var _ driver.Fetcher = testFetcher{}
