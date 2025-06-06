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
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"math/big"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/google/pprof/internal/binutils"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/proftest"
	"github.com/google/pprof/internal/symbolizer"
	"github.com/google/pprof/internal/transport"
	"github.com/google/pprof/profile"
)

func TestSymbolizationPath(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("test assumes Unix paths")
	}

	// Save environment variables to restore after test
	saveHome := os.Getenv(homeEnv())
	savePath := os.Getenv("PPROF_BINARY_PATH")

	tempdir, err := os.MkdirTemp("", "home")
	if err != nil {
		t.Fatal("creating temp dir: ", err)
	}
	defer os.RemoveAll(tempdir)
	os.MkdirAll(filepath.Join(tempdir, "pprof", "binaries", "abcde10001"), 0700)
	os.Create(filepath.Join(tempdir, "pprof", "binaries", "abcde10001", "binary"))

	os.MkdirAll(filepath.Join(tempdir, "pprof", "binaries", "fg"), 0700)
	os.Create(filepath.Join(tempdir, "pprof", "binaries", "fg", "hij10001.debug"))

	obj := testObj{tempdir}
	os.Setenv(homeEnv(), tempdir)
	for _, tc := range []struct {
		env, file, buildID, want string
		msgCount                 int
	}{
		{"", "/usr/bin/binary", "", "/usr/bin/binary", 0},
		{"", "/usr/bin/binary", "fedcb10000", "/usr/bin/binary", 0},
		{"/usr", "/bin/binary", "", "/usr/bin/binary", 0},
		{"", "/prod/path/binary", "abcde10001", filepath.Join(tempdir, "pprof/binaries/abcde10001/binary"), 0},
		{"/alternate/architecture", "/usr/bin/binary", "", "/alternate/architecture/binary", 0},
		{"/alternate/architecture", "/usr/bin/binary", "abcde10001", "/alternate/architecture/binary", 0},
		{"", "", "fghij10001", filepath.Join(tempdir, "pprof/binaries/fg/hij10001.debug"), 0},
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
		locateBinaries(p, s, obj, &proftest.TestUI{T: t, Ignore: tc.msgCount})
		if file := p.Mapping[0].File; file != tc.want {
			t.Errorf("%s:%s:%s, want %s, got %s", tc.env, tc.file, tc.buildID, tc.want, file)
		}
	}
	os.Setenv(homeEnv(), saveHome)
	os.Setenv("PPROF_BINARY_PATH", savePath)
}

func TestCollectMappingSources(t *testing.T) {
	const startAddress uint64 = 0x40000
	const url = "http://example.com"
	for _, tc := range []struct {
		file, buildID string
		want          plugin.MappingSources
	}{
		{"/usr/bin/binary", "buildId", mappingSources("buildId", url, startAddress)},
		{"/usr/bin/binary", "", mappingSources("/usr/bin/binary", url, startAddress)},
		{"", "", mappingSources(url, url, startAddress)},
	} {
		p := &profile.Profile{
			Mapping: []*profile.Mapping{
				{
					File:    tc.file,
					BuildID: tc.buildID,
					Start:   startAddress,
				},
			},
		}
		got := collectMappingSources(p, url)
		if !reflect.DeepEqual(got, tc.want) {
			t.Errorf("%s:%s, want %v, got %v", tc.file, tc.buildID, tc.want, got)
		}
	}
}

func TestUnsourceMappings(t *testing.T) {
	for _, tc := range []struct {
		os, file, buildID, want string
	}{
		{"any", "/usr/bin/binary", "buildId", "/usr/bin/binary"},
		{"any", "http://example.com", "", ""},
		{"windows", `C:\example.exe`, "", `C:\example.exe`},
		{"windows", `c:/example.exe`, "", `c:/example.exe`},
	} {
		t.Run(tc.file+"-"+tc.os, func(t *testing.T) {
			if tc.os != "any" && tc.os != runtime.GOOS {
				t.Skipf("%s only test", tc.os)
			}

			p := &profile.Profile{
				Mapping: []*profile.Mapping{
					{
						File:    tc.file,
						BuildID: tc.buildID,
					},
				},
			}
			unsourceMappings(p)
			if got := p.Mapping[0].File; got != tc.want {
				t.Errorf("%s:%s, want %s, got %s", tc.file, tc.buildID, tc.want, got)
			}
		})
	}
}

type testObj struct {
	home string
}

func (o testObj) Open(file string, start, limit, offset uint64, relocationSymbol string) (plugin.ObjFile, error) {
	switch file {
	case "/alternate/architecture/binary":
		return testFile{file, "abcde10001"}, nil
	case "/usr/bin/binary":
		return testFile{file, "fedcb10000"}, nil
	case filepath.Join(o.home, "pprof/binaries/abcde10001/binary"):
		return testFile{file, "abcde10001"}, nil
	case filepath.Join(o.home, "pprof/binaries/fg/hij10001.debug"):
		return testFile{file, "fghij10001"}, nil
	}
	return nil, fmt.Errorf("not found: %s", file)
}
func (testObj) Demangler(_ string) func(names []string) (map[string]string, error) {
	return func(names []string) (map[string]string, error) { return nil, nil }
}
func (testObj) Disasm(file string, start, end uint64, intelSyntax bool) ([]plugin.Inst, error) {
	return nil, nil
}

type testFile struct{ name, buildID string }

func (f testFile) Name() string                                               { return f.name }
func (testFile) ObjAddr(addr uint64) (uint64, error)                          { return addr, nil }
func (f testFile) BuildID() string                                            { return f.buildID }
func (testFile) SourceLine(addr uint64) ([]plugin.Frame, error)               { return nil, nil }
func (testFile) Symbols(r *regexp.Regexp, addr uint64) ([]*plugin.Sym, error) { return nil, nil }
func (testFile) Close() error                                                 { return nil }

func TestFetch(t *testing.T) {
	const path = "testdata/"
	type testcase struct {
		source, execName string
		wantErr          bool
	}
	ts := []testcase{
		{path + "go.crc32.cpu", "", false},
		{path + "go.nomappings.crash", "/bin/gotest.exe", false},
		{"http://localhost/profile?file=cppbench.cpu", "", false},
		{"./missing", "", true},
	}
	// Test that paths with a colon character are recognized as file paths
	// if the file exists, rather than as a URL. We have to skip this test
	// on Windows since the colon char is not allowed in Windows paths.
	if runtime.GOOS != "windows" {
		src := filepath.Join(path, "go.crc32.cpu")
		dst := filepath.Join(t.TempDir(), "go.crc32.cpu_2023-11-11_01:02:03")
		data, err := os.ReadFile(src)
		if err != nil {
			t.Fatalf("read src file %s failed: %#v", src, err)
		}
		err = os.WriteFile(dst, data, 0644)
		if err != nil {
			t.Fatalf("create dst file %s failed: %#v", dst, err)
		}
		ts = append(ts, testcase{dst, "", false})
	}
	for _, tc := range ts {
		t.Run(tc.source, func(t *testing.T) {
			p, _, _, err := grabProfile(&source{ExecName: tc.execName}, tc.source, nil, testObj{}, &proftest.TestUI{T: t}, &httpTransport{})
			if tc.wantErr {
				if err == nil {
					t.Fatal("got no error, want an error")
				}
				return
			}
			if err != nil {
				t.Fatalf("got error %v, want no error", err)
			}
			if len(p.Sample) == 0 {
				t.Error("got zero samples, want non-zero")
			}
			if e := tc.execName; e != "" {
				switch {
				case len(p.Mapping) == 0 || p.Mapping[0] == nil:
					t.Errorf("got no mappings, want mapping[0].execName == %s", e)
				case p.Mapping[0].File != e:
					t.Errorf("got mapping[0].execName == %s, want %s", p.Mapping[0].File, e)
				}
			}
		})
	}
}

func TestFetchWithBase(t *testing.T) {
	baseConfig := currentConfig()
	defer setCurrentConfig(baseConfig)

	type WantSample struct {
		values []int64
		labels map[string][]string
	}

	const path = "testdata/"
	type testcase struct {
		desc              string
		sources           []string
		bases             []string
		diffBases         []string
		normalize         bool
		wantSamples       []WantSample
		wantParseErrorMsg string
		wantFetchErrorMsg string
	}

	testcases := []testcase{
		{
			"not normalized base is same as source",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention"},
			nil,
			false,
			nil,
			"",
			"",
		},
		{
			"not normalized base is same as source",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention"},
			nil,
			false,
			nil,
			"",
			"",
		},
		{
			"not normalized single source, multiple base (all profiles same)",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention", path + "cppbench.contention"},
			nil,
			false,
			[]WantSample{
				{
					values: []int64{-2700, -608881724},
					labels: map[string][]string{},
				},
				{
					values: []int64{-100, -23992},
					labels: map[string][]string{},
				},
				{
					values: []int64{-200, -179943},
					labels: map[string][]string{},
				},
				{
					values: []int64{-100, -17778444},
					labels: map[string][]string{},
				},
				{
					values: []int64{-100, -75976},
					labels: map[string][]string{},
				},
				{
					values: []int64{-300, -63568134},
					labels: map[string][]string{},
				},
			},
			"",
			"",
		},
		{
			"not normalized, different base and source",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.small.contention"},
			nil,
			false,
			[]WantSample{
				{
					values: []int64{1700, 608878600},
					labels: map[string][]string{},
				},
				{
					values: []int64{100, 23992},
					labels: map[string][]string{},
				},
				{
					values: []int64{200, 179943},
					labels: map[string][]string{},
				},
				{
					values: []int64{100, 17778444},
					labels: map[string][]string{},
				},
				{
					values: []int64{100, 75976},
					labels: map[string][]string{},
				},
				{
					values: []int64{300, 63568134},
					labels: map[string][]string{},
				},
			},
			"",
			"",
		},
		{
			"normalized base is same as source",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention"},
			nil,
			true,
			nil,
			"",
			"",
		},
		{
			"normalized single source, multiple base (all profiles same)",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention", path + "cppbench.contention"},
			nil,
			true,
			nil,
			"",
			"",
		},
		{
			"normalized different base and source",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.small.contention"},
			nil,
			true,
			[]WantSample{
				{
					values: []int64{-229, -369},
					labels: map[string][]string{},
				},
				{
					values: []int64{29, 0},
					labels: map[string][]string{},
				},
				{
					values: []int64{57, 1},
					labels: map[string][]string{},
				},
				{
					values: []int64{29, 80},
					labels: map[string][]string{},
				},
				{
					values: []int64{29, 0},
					labels: map[string][]string{},
				},
				{
					values: []int64{86, 288},
					labels: map[string][]string{},
				},
			},
			"",
			"",
		},
		{
			"not normalized diff base is same as source",
			[]string{path + "cppbench.contention"},
			nil,
			[]string{path + "cppbench.contention"},
			false,
			[]WantSample{
				{
					values: []int64{2700, 608881724},
					labels: map[string][]string{},
				},
				{
					values: []int64{100, 23992},
					labels: map[string][]string{},
				},
				{
					values: []int64{200, 179943},
					labels: map[string][]string{},
				},
				{
					values: []int64{100, 17778444},
					labels: map[string][]string{},
				},
				{
					values: []int64{100, 75976},
					labels: map[string][]string{},
				},
				{
					values: []int64{300, 63568134},
					labels: map[string][]string{},
				},
				{
					values: []int64{-2700, -608881724},
					labels: map[string][]string{"pprof::base": {"true"}},
				},
				{
					values: []int64{-100, -23992},
					labels: map[string][]string{"pprof::base": {"true"}},
				},
				{
					values: []int64{-200, -179943},
					labels: map[string][]string{"pprof::base": {"true"}},
				},
				{
					values: []int64{-100, -17778444},
					labels: map[string][]string{"pprof::base": {"true"}},
				},
				{
					values: []int64{-100, -75976},
					labels: map[string][]string{"pprof::base": {"true"}},
				},
				{
					values: []int64{-300, -63568134},
					labels: map[string][]string{"pprof::base": {"true"}},
				},
			},
			"",
			"",
		},
		{
			"diff_base and base both specified",
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention"},
			[]string{path + "cppbench.contention"},
			false,
			nil,
			"-base and -diff_base flags cannot both be specified",
			"",
		},
		{
			"input profiles with different sample types (non empty intersection)",
			[]string{path + "cppbench.cpu", path + "cppbench.cpu_no_samples_type"},
			[]string{path + "cppbench.cpu", path + "cppbench.cpu_no_samples_type"},
			nil,
			false,
			nil,
			"",
			"",
		},
		{
			"input profiles with different sample types (empty intersection)",
			[]string{path + "cppbench.cpu", path + "cppbench.contention"},
			[]string{path + "cppbench.cpu", path + "cppbench.contention"},
			nil,
			false,
			nil,
			"",
			"problem fetching source profiles: profiles have empty common sample type list",
		},
	}

	for _, tc := range testcases {
		t.Run(tc.desc, func(t *testing.T) {
			setCurrentConfig(baseConfig)
			f := testFlags{
				stringLists: map[string][]string{
					"base":      tc.bases,
					"diff_base": tc.diffBases,
				},
				bools: map[string]bool{
					"normalize": tc.normalize,
				},
			}
			f.args = tc.sources

			o := setDefaults(&plugin.Options{
				UI:            &proftest.TestUI{T: t, AllowRx: "Local symbolization failed|Some binary filenames not available"},
				Flagset:       f,
				HTTPTransport: transport.New(nil),
			})
			src, _, err := parseFlags(o)

			if tc.wantParseErrorMsg != "" {
				if err == nil {
					t.Fatalf("got nil, want error %q", tc.wantParseErrorMsg)
				}

				if gotErrMsg := err.Error(); gotErrMsg != tc.wantParseErrorMsg {
					t.Fatalf("got error %q, want error %q", gotErrMsg, tc.wantParseErrorMsg)
				}
				return
			}

			if err != nil {
				t.Fatalf("got error %q, want no error", err)
			}

			p, err := fetchProfiles(src, o)

			if tc.wantFetchErrorMsg != "" {
				if err == nil {
					t.Fatalf("got nil, want error %q", tc.wantFetchErrorMsg)
				}

				if gotErrMsg := err.Error(); gotErrMsg != tc.wantFetchErrorMsg {
					t.Fatalf("got error %q, want error %q", gotErrMsg, tc.wantFetchErrorMsg)
				}
				return
			}

			if err != nil {
				t.Fatalf("got error %q, want no error", err)
			}

			if got, want := len(p.Sample), len(tc.wantSamples); got != want {
				t.Fatalf("got %d samples want %d", got, want)
			}

			for i, sample := range p.Sample {
				if !reflect.DeepEqual(tc.wantSamples[i].values, sample.Value) {
					t.Errorf("for sample %d got values %v, want %v", i, sample.Value, tc.wantSamples[i])
				}
				if !reflect.DeepEqual(tc.wantSamples[i].labels, sample.Label) {
					t.Errorf("for sample %d got labels %v, want %v", i, sample.Label, tc.wantSamples[i].labels)
				}
			}
		})
	}
}

// mappingSources creates MappingSources map with a single item.
func mappingSources(key, source string, start uint64) plugin.MappingSources {
	return plugin.MappingSources{
		key: []struct {
			Source string
			Start  uint64
		}{
			{Source: source, Start: start},
		},
	}
}

type httpTransport struct{}

func (tr *httpTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	values := req.URL.Query()
	file := values.Get("file")

	if file == "" {
		return nil, fmt.Errorf("want .../file?profile, got %s", req.URL.String())
	}

	t := &http.Transport{}
	t.RegisterProtocol("file", http.NewFileTransport(http.Dir("testdata/")))

	c := &http.Client{Transport: t}
	return c.Get("file:///" + file)
}

func closedError() string {
	if runtime.GOOS == "plan9" {
		return "listen hungup"
	}
	return "use of closed"
}

func TestHTTPSInsecure(t *testing.T) {
	if runtime.GOOS == "nacl" || runtime.GOOS == "js" {
		t.Skip("test assumes tcp available")
	}
	saveHome := os.Getenv(homeEnv())
	tempdir, err := os.MkdirTemp("", "home")
	if err != nil {
		t.Fatal("creating temp dir: ", err)
	}
	defer os.RemoveAll(tempdir)

	// pprof writes to $HOME/pprof by default which is not necessarily
	// writeable (e.g. on a Debian build) so set $HOME to something we
	// know we can write to for the duration of the test.
	os.Setenv(homeEnv(), tempdir)
	defer os.Setenv(homeEnv(), saveHome)

	baseConfig := currentConfig()
	defer setCurrentConfig(baseConfig)

	tlsCert, _, _ := selfSignedCert(t, "")
	tlsConfig := &tls.Config{Certificates: []tls.Certificate{tlsCert}}

	l, err := tls.Listen("tcp", "localhost:0", tlsConfig)
	if err != nil {
		t.Fatalf("net.Listen: got error %v, want no error", err)
	}

	donec := make(chan error, 1)
	go func(donec chan<- error) {
		donec <- http.Serve(l, nil)
	}(donec)
	defer func() {
		if got, want := <-donec, closedError(); !strings.Contains(got.Error(), want) {
			t.Fatalf("Serve got error %v, want %q", got, want)
		}
	}()
	defer l.Close()

	outputTempFile, err := os.CreateTemp("", "profile_output")
	if err != nil {
		t.Fatalf("Failed to create tempfile: %v", err)
	}
	defer os.Remove(outputTempFile.Name())
	defer outputTempFile.Close()

	address := "https+insecure://" + l.Addr().String() + "/debug/pprof/goroutine"
	s := &source{
		Sources:   []string{address},
		Timeout:   10,
		Symbolize: "remote",
	}
	o := &plugin.Options{
		Obj:           &binutils.Binutils{},
		UI:            &proftest.TestUI{T: t, AllowRx: "Saved profile in"},
		HTTPTransport: transport.New(nil),
	}
	o.Sym = &symbolizer.Symbolizer{Obj: o.Obj, UI: o.UI}
	p, err := fetchProfiles(s, o)
	if err != nil {
		t.Fatal(err)
	}
	if len(p.SampleType) == 0 {
		t.Fatalf("fetchProfiles(%s) got empty profile: len(p.SampleType)==0", address)
	}
	if len(p.Function) == 0 {
		t.Fatalf("fetchProfiles(%s) got non-symbolized profile: len(p.Function)==0", address)
	}
	if err := checkProfileHasFunction(p, "TestHTTPSInsecure"); err != nil {
		t.Fatalf("fetchProfiles(%s) %v", address, err)
	}
}

func TestHTTPSWithServerCertFetch(t *testing.T) {
	if runtime.GOOS == "nacl" || runtime.GOOS == "js" {
		t.Skip("test assumes tcp available")
	}
	saveHome := os.Getenv(homeEnv())
	tempdir, err := os.MkdirTemp("", "home")
	if err != nil {
		t.Fatal("creating temp dir: ", err)
	}
	defer os.RemoveAll(tempdir)

	// pprof writes to $HOME/pprof by default which is not necessarily
	// writeable (e.g. on a Debian build) so set $HOME to something we
	// know we can write to for the duration of the test.
	os.Setenv(homeEnv(), tempdir)
	defer os.Setenv(homeEnv(), saveHome)

	baseConfig := currentConfig()
	defer setCurrentConfig(baseConfig)

	cert, certBytes, keyBytes := selfSignedCert(t, "localhost")
	cas := x509.NewCertPool()
	cas.AppendCertsFromPEM(certBytes)

	tlsConfig := &tls.Config{
		RootCAs:      cas,
		Certificates: []tls.Certificate{cert},
		ClientAuth:   tls.RequireAndVerifyClientCert,
		ClientCAs:    cas,
	}

	l, err := tls.Listen("tcp", "localhost:0", tlsConfig)
	if err != nil {
		t.Fatalf("net.Listen: got error %v, want no error", err)
	}

	donec := make(chan error, 1)
	go func(donec chan<- error) {
		donec <- http.Serve(l, nil)
	}(donec)
	defer func() {
		if got, want := <-donec, closedError(); !strings.Contains(got.Error(), want) {
			t.Fatalf("Serve got error %v, want %q", got, want)
		}
	}()
	defer l.Close()

	outputTempFile, err := os.CreateTemp("", "profile_output")
	if err != nil {
		t.Fatalf("Failed to create tempfile: %v", err)
	}
	defer os.Remove(outputTempFile.Name())
	defer outputTempFile.Close()

	// Get port from the address, so request to the server can be made using
	// the host name specified in certificates.
	_, portStr, err := net.SplitHostPort(l.Addr().String())
	if err != nil {
		t.Fatalf("cannot get port from URL: %v", err)
	}
	address := "https://" + "localhost:" + portStr + "/debug/pprof/goroutine"
	s := &source{
		Sources:   []string{address},
		Timeout:   10,
		Symbolize: "remote",
	}

	certTempFile, err := os.CreateTemp("", "cert_output")
	if err != nil {
		t.Errorf("cannot create cert tempfile: %v", err)
	}
	defer os.Remove(certTempFile.Name())
	defer certTempFile.Close()
	certTempFile.Write(certBytes)

	keyTempFile, err := os.CreateTemp("", "key_output")
	if err != nil {
		t.Errorf("cannot create key tempfile: %v", err)
	}
	defer os.Remove(keyTempFile.Name())
	defer keyTempFile.Close()
	keyTempFile.Write(keyBytes)

	f := &testFlags{
		strings: map[string]string{
			"tls_cert": certTempFile.Name(),
			"tls_key":  keyTempFile.Name(),
			"tls_ca":   certTempFile.Name(),
		},
	}
	o := &plugin.Options{
		Obj:           &binutils.Binutils{},
		UI:            &proftest.TestUI{T: t, AllowRx: "Saved profile in"},
		Flagset:       f,
		HTTPTransport: transport.New(f),
	}

	o.Sym = &symbolizer.Symbolizer{Obj: o.Obj, UI: o.UI, Transport: o.HTTPTransport}
	p, err := fetchProfiles(s, o)
	if err != nil {
		t.Fatal(err)
	}
	if len(p.SampleType) == 0 {
		t.Fatalf("fetchProfiles(%s) got empty profile: len(p.SampleType)==0", address)
	}
	if len(p.Function) == 0 {
		t.Fatalf("fetchProfiles(%s) got non-symbolized profile: len(p.Function)==0", address)
	}
	if err := checkProfileHasFunction(p, "TestHTTPSWithServerCertFetch"); err != nil {
		t.Fatalf("fetchProfiles(%s) %v", address, err)
	}
}

func checkProfileHasFunction(p *profile.Profile, fname string) error {
	for _, f := range p.Function {
		if strings.Contains(f.Name, fname) {
			return nil
		}
	}
	return fmt.Errorf("got %s, want function %q", p.String(), fname)
}

// selfSignedCert generates a self-signed certificate, and returns the
// generated certificate, and byte arrays containing the certificate and
// key associated with the certificate.
func selfSignedCert(t *testing.T, host string) (tls.Certificate, []byte, []byte) {
	privKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("failed to generate private key: %v", err)
	}
	b, err := x509.MarshalECPrivateKey(privKey)
	if err != nil {
		t.Fatalf("failed to marshal private key: %v", err)
	}
	bk := pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: b})

	tmpl := x509.Certificate{
		SerialNumber: big.NewInt(1),
		NotBefore:    time.Now(),
		NotAfter:     time.Now().Add(10 * time.Minute),
		IsCA:         true,
		DNSNames:     []string{host},
	}

	b, err = x509.CreateCertificate(rand.Reader, &tmpl, &tmpl, privKey.Public(), privKey)
	if err != nil {
		t.Fatalf("failed to create cert: %v", err)
	}
	bc := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: b})

	cert, err := tls.X509KeyPair(bc, bk)
	if err != nil {
		t.Fatalf("failed to create TLS key pair: %v", err)
	}
	return cert, bc, bk
}
