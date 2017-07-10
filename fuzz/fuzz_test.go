package pprof

import (
	"github.com/google/pprof/profile"
	"io/ioutil"
	"testing"
)

func TestParseData(t *testing.T) {
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
