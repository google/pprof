package pprof

import(
	"testing"
	"io/ioutil"
	"github.com/google/pprof/profile"
)

func TestParseData(t *testing.T) {
	const path = "test_corpus/"
	files, err := ioutil.ReadDir(path)
	if err != nil {
		t.Errorf("Problem reading files in directory " + path + ":", err)
	}
	for _, f := range files {
		file := path + f.Name()
		inbytes, err := ioutil.ReadFile(file)
		if err != nil {
			t.Errorf("Problem reading file: " + file, err)
			continue
		}
		profile.ParseData(inbytes)
	}
}