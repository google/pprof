package proftest

import (
	"bytes"
	"testing"
)

func TestDiffEqual(t *testing.T) {
	for _, tc := range []struct {
		name   string
		b1, b2 []byte
	}{
		{
			name: "both nil",
			b1:   nil,
			b2:   nil,
		},
		{
			name: "both empty",
			b1:   []byte(""),
			b2:   []byte(""),
		},
		{
			name: "equal non-empty content",
			b1:   []byte("line 1\nline 2\n"),
			b2:   []byte("line 1\nline 2\n"),
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			diff, err := Diff(tc.b1, tc.b2)
			if err != nil {
				t.Fatalf("Diff unexpected error: %v", err)
			}
			if len(diff) != 0 {
				t.Errorf("Diff got non-empty diff %q; want empty", string(diff))
			}
		})
	}
}

func TestDiffUnequal(t *testing.T) {
	b1 := []byte("hello\n")
	b2 := []byte("world\n")

	diff, err := Diff(b1, b2)
	if err != nil {
		t.Fatalf("Diff unexpected error: %v", err)
	}
	if len(diff) == 0 {
		t.Fatalf("Diff got empty diff; want non-empty")
	}
	// The diff output should either contain diff output or fallback report
	if !bytes.Contains(diff, []byte("hello")) || !bytes.Contains(diff, []byte("world")) {
		t.Errorf("Diff output %q does not mention inputs", string(diff))
	}
}
