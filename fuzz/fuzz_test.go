// Package is used in conjunction with github.com/dvyukov/go-fuzz/go-fuzz
// to fuzz ParseData function

package pprof

import (
	"io/ioutil"
	"testing"

	"github.com/google/pprof/profile"
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
