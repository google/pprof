package driver

import (
	"reflect"
	"strings"
	"testing"
)

// TestBrowsersForOS checks the argument list each browser command expands to,
// the way invokeVisualizer and openBrowser build it.
func TestBrowsersForOS(t *testing.T) {
	// A target that contains a space is what exposes the Windows behavior: start
	// takes the first quoted argument as the window title, so an empty title has
	// to precede it.
	const target = `C:\Users\Ada Lovelace\AppData\Local\Temp\pprof001.svg`

	for _, tc := range []struct {
		goos string
		want []string
	}{
		{goos: "windows", want: []string{"cmd", "/c", "start", "", target}},
		{goos: "darwin", want: []string{"/usr/bin/open", target}},
	} {
		t.Setenv("BROWSER", "")
		cmds := browsersForOS(tc.goos)
		if len(cmds) != 1 {
			t.Fatalf("browsersForOS(%q) returned %d commands; want 1", tc.goos, len(cmds))
		}
		got := append(strings.Split(cmds[0], " "), target)
		if !reflect.DeepEqual(got, tc.want) {
			t.Errorf("browsersForOS(%q) expands to %q; want %q", tc.goos, got, tc.want)
		}
	}
}
