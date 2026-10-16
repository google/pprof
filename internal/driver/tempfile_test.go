package driver

import (
	"os"
	"runtime"
	"sync"
	"testing"
)

func TestNewTempFile(t *testing.T) {
	const n = 100
	// Line up ready to execute goroutines with a read-write lock.
	var mu sync.RWMutex
	mu.Lock()
	var wg sync.WaitGroup
	errc := make(chan error, n)
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func() {
			mu.RLock()
			defer mu.RUnlock()
			defer wg.Done()
			f, err := newTempFile(os.TempDir(), "profile", ".tmp")
			errc <- err
			deferDeleteTempFile(f.Name())
			f.Close()
		}()
	}
	// Start the file creation race.
	mu.Unlock()
	// Wait for the goroutines to finish.
	wg.Wait()

	for i := 0; i < n; i++ {
		if err := <-errc; err != nil {
			t.Fatalf("newTempFile(): got %v, want no error", err)
		}
	}
	if len(tempFiles) != n {
		t.Errorf("len(tempFiles): got %d, want %d", len(tempFiles), n)
	}
	names := map[string]bool{}
	for _, name := range tempFiles {
		if names[name] {
			t.Errorf("got temp file %s created multiple times", name)
			break
		}
		names[name] = true
		// Temp files can hold profile data, so they must not be accessible
		// by group or other users.
		if fi, err := os.Stat(name); err != nil {
			t.Errorf("Stat(%s): got error %v, want no error", name, err)
		} else if perm := fi.Mode().Perm(); runtime.GOOS != "windows" && perm&0077 != 0 {
			t.Errorf("temp file %s has mode %#o, want no group/other access", name, perm)
		}
	}
	if err := cleanupTempFiles(); err != nil {
		t.Errorf("cleanupTempFiles(): got error %v, want no error", err)
	}
	if len(tempFiles) != 0 {
		t.Errorf("len(tempFiles) after the cleanup: got %d, want 0", len(tempFiles))
	}
}
