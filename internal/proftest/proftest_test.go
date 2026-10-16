package proftest

import (
	"testing"
)

func TestDiff(t *testing.T) {
	for _, tc := range []struct {
		name      string
		b1, b2    []byte
		emptyPATH bool
		wantDiff  bool
	}{
		{
			name:     "both nil",
			b1:       nil,
			b2:       nil,
			wantDiff: false,
		},
		{
			name:     "both empty",
			b1:       []byte(""),
			b2:       []byte(""),
			wantDiff: false,
		},
		{
			name:     "equal non-empty content",
			b1:       []byte("line 1\nline 2\n"),
			b2:       []byte("line 1\nline 2\n"),
			wantDiff: false,
		},
		{
			name:      "equal non-empty content without diff in PATH",
			b1:        []byte("line 1\nline 2\n"),
			b2:        []byte("line 1\nline 2\n"),
			emptyPATH: true,
			wantDiff:  false,
		},
		{
			name:     "unequal content",
			b1:       []byte("hello\n"),
			b2:       []byte("world\n"),
			wantDiff: true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if tc.emptyPATH {
				t.Setenv("PATH", "")
			}
			diff, err := Diff(tc.b1, tc.b2)
			if err != nil {
				t.Fatalf("Diff unexpected error: %v", err)
			}
			if (len(diff) != 0) != tc.wantDiff {
				t.Errorf("Diff(%q, %q) got diff len=%d, wantDiff=%v\ndiff: %s", tc.b1, tc.b2, len(diff), tc.wantDiff, diff)
			}
		})
	}
}
