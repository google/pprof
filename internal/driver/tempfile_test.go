package driver

import (
	"os"
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
	}
	if err := cleanupTempFiles(); err != nil {
		t.Errorf("cleanupTempFiles(): got error %v, want no error", err)
	}
	if len(tempFiles) != 0 {
		t.Errorf("len(tempFiles) after the cleanup: got %d, want 0", len(tempFiles))
	}
}
