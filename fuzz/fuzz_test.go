package pprof

import (
	"io/ioutil"
	"runtime"
	"testing"

	"github.com/google/pprof/profile"
)

func TestParseData(t *testing.T) {
	if runtime.GOOS == "nacl" {
		t.Skip("no direct filesystem access on Nacl")
	}

	const path = "testdata/"
	files, err := ioutil.ReadDir(path)
	if err != nil {
		t.Errorf("Problem reading directory %s : %v", path, err)
	}
	for _, f := range files {
		file := path + f.Name()
		inbytes, err := ioutil.ReadFile(file)
		if err != nil {
			t.Errorf("Problem reading file: %s : %v", file, err)
			continue
		}
		profile.ParseData(inbytes)
	}
}
