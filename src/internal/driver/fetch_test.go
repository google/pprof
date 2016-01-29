// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package driver

import (
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"testing"
	"time"

	"internal/plugin"
	"internal/proftest"
	"profile"
)

func TestSymbolizationPath(t *testing.T) {
	// Save environment variables to restore after test
	saveHome := os.Getenv("HOME")
	savePath := os.Getenv("PPROF_BINARY_PATH")

	tempdir, err := ioutil.TempDir("", "home")
	if err != nil {
		t.Fatal("creating temp dir: ", err)
	}
	defer os.RemoveAll(tempdir)
	os.MkdirAll(filepath.Join(tempdir, "pprof", "binaries", "abcde10001"), 0700)
	os.Create(filepath.Join(tempdir, "pprof", "binaries", "abcde10001", "binary"))

	obj := testObj{tempdir}
	os.Setenv("HOME", tempdir)
	for _, tc := range []struct {
		env, file, buildID, want string
		msgCount                 int
	}{
		{"", "/usr/bin/binary", "", "/usr/bin/binary", 0},
		{"", "/usr/bin/binary", "fedcb10000", "/usr/bin/binary", 0},
		{"", "/prod/path/binary", "abcde10001", filepath.Join(tempdir, "pprof/binaries/abcde10001/binary"), 0},
		{"/alternate/architecture", "/usr/bin/binary", "", "/alternate/architecture/binary", 0},
		{"/alternate/architecture", "/usr/bin/binary", "abcde10001", "/alternate/architecture/binary", 0},
		{"/nowhere:/alternate/architecture", "/usr/bin/binary", "fedcb10000", "/usr/bin/binary", 1},
		{"/nowhere:/alternate/architecture", "/usr/bin/binary", "abcde10002", "/usr/bin/binary", 1},
	} {
		os.Setenv("PPROF_BINARY_PATH", tc.env)
		p := &profile.Profile{
			Mapping: []*profile.Mapping{
				{
					File:    tc.file,
					BuildID: tc.buildID,
				},
			},
		}
		s := &source{}
		locateBinaries(p, s, obj, &proftest.TestUI{t, tc.msgCount})
		if file := p.Mapping[0].File; file != tc.want {
			t.Errorf("%s:%s:%s, want %s, got %s", tc.env, tc.file, tc.buildID, tc.want, file)
		}
	}
	os.Setenv("HOME", saveHome)
	os.Setenv("PPROF_BINARY_PATH", savePath)
}

type testObj struct {
	home string
}

func (o testObj) Open(file string, start, limit, offset uint64) (plugin.ObjFile, error) {
	switch file {
	case "/alternate/architecture/binary":
		return testFile{file, "abcde10001"}, nil
	case "/usr/bin/binary":
		return testFile{file, "fedcb10000"}, nil
	case filepath.Join(o.home, "pprof/binaries/abcde10001/binary"):
		return testFile{file, "abcde10001"}, nil
	}
	return nil, fmt.Errorf("not found: %s", file)
}
func (testObj) Demangler(_ string) func(names []string) (map[string]string, error) {
	return func(names []string) (map[string]string, error) { return nil, nil }
}
func (testObj) Disasm(file string, start, end uint64) ([]plugin.Inst, error) { return nil, nil }

type testFile struct{ name, buildID string }

func (f testFile) Name() string                                               { return f.name }
func (testFile) Base() uint64                                                 { return 0 }
func (f testFile) BuildID() string                                            { return f.buildID }
func (testFile) SourceLine(addr uint64) ([]plugin.Frame, error)               { return nil, nil }
func (testFile) Symbols(r *regexp.Regexp, addr uint64) ([]*plugin.Sym, error) { return nil, nil }
func (testFile) Close() error                                                 { return nil }

func TestFetch(t *testing.T) {
	const path = "testdata/"

	// Intercept http.Get calls from HTTPFetcher.
	httpGet = stubHTTPGet

	for _, source := range [][2]string{
		{path + "go.crc32.cpu", "go.crc32.cpu"},
		{"http://localhost/profile?file=cppbench.cpu", "cppbench.cpu"},
	} {
		p, _, err := fetch(source[0], 0, 10*time.Second, &proftest.TestUI{t, 0})
		if err != nil {
			t.Fatalf("%s: %s", source[0], err)
		}
		if len(p.Sample) == 0 {
			t.Errorf("want non-zero samples")
		}
	}
}

// stubHTTPGet intercepts a call to http.Get and rewrites it to use
// "file://" to get the profile directly from a file.
func stubHTTPGet(source string, _ time.Duration) (*http.Response, error) {
	url, err := url.Parse(source)
	if err != nil {
		return nil, err
	}

	values := url.Query()
	file := values.Get("file")

	if file == "" {
		return nil, fmt.Errorf("want .../file?profile, got %s", source)
	}

	t := &http.Transport{}
	t.RegisterProtocol("file", http.NewFileTransport(http.Dir("testdata/")))

	c := &http.Client{Transport: t}
	return c.Get("file:///" + file)
}
