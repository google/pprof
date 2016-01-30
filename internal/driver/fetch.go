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
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"time"

	"github.com/google/pprof/internal/measurement"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

// fetchProfiles fetches and symbolizes the profiles specified by s.
// It will merge all the profiles it is able to retrieve, even if
// there are some failures. It will return an error if it is unable to
// fetch any profiles.
func fetchProfiles(s *source, o *plugin.Options) (*profile.Profile, error) {
	if err := setTmpDir(o.UI); err != nil {
		return nil, err
	}

	p, msrcs, save, err := concurrentGrab(s, o.Fetch, o.Obj, o.UI)
	if err != nil {
		return nil, err
	}

	// Symbolize the merged profile.
	if err := o.Sym.Symbolize(s.Symbolize, msrcs, p); err != nil {
		return nil, err
	}
	p.RemoveUninteresting()

	// Save a copy of the merged profile if there is at least one remote source.
	if save {
		prefix := "pprof."
		if len(p.Mapping) > 0 && p.Mapping[0].File != "" {
			prefix += filepath.Base(p.Mapping[0].File) + "."
		}
		for _, s := range p.SampleType {
			prefix += s.Type + "."
		}

		dir := os.Getenv("PPROF_TMPDIR")
		tempFile, err := newTempFile(dir, prefix, ".pb.gz")
		if err == nil {
			if err = p.Write(tempFile); err == nil {
				o.UI.PrintErr("Saved profile in ", tempFile.Name())
			}
		}
		if err != nil {
			o.UI.PrintErr("Could not save profile: ", err)
		}
	}

	if err := p.CheckValid(); err != nil {
		return nil, err
	}

	return p, nil
}

// concurrentGrab fetches multiple profiles concurrently
func concurrentGrab(s *source, fetch plugin.Fetcher, obj plugin.ObjTool, ui plugin.UI) (*profile.Profile, plugin.MappingSources, bool, error) {
	wg := sync.WaitGroup{}
	numprofs := len(s.Sources) + len(s.Base)
	profs := make([]*profile.Profile, numprofs)
	msrcs := make([]plugin.MappingSources, numprofs)
	remote := make([]bool, numprofs)
	errs := make([]error, numprofs)
	for i, source := range s.Sources {
		wg.Add(1)
		go func(i int, src string) {
			defer wg.Done()
			profs[i], msrcs[i], remote[i], errs[i] = grabProfile(s, src, 1, fetch, obj, ui)
		}(i, source)
	}
	for i, source := range s.Base {
		wg.Add(1)
		go func(i int, src string) {
			defer wg.Done()
			profs[i], msrcs[i], remote[i], errs[i] = grabProfile(s, src, -1, fetch, obj, ui)
		}(i+len(s.Sources), source)
	}
	wg.Wait()
	var save bool
	var numFailed = 0
	for i, src := range s.Sources {
		if errs[i] != nil {
			ui.PrintErr(src + ": " + errs[i].Error())
			numFailed++
		}
		save = save || remote[i]
	}
	for i, src := range s.Base {
		b := i + len(s.Sources)
		if errs[b] != nil {
			ui.PrintErr(src + ": " + errs[b].Error())
			numFailed++
		}
		save = save || remote[b]
	}
	if numFailed == numprofs {
		return nil, nil, false, fmt.Errorf("failed to fetch any profiles")
	}
	if numFailed > 0 {
		ui.PrintErr(fmt.Sprintf("fetched %d profiles out of %d", numprofs-numFailed, numprofs))
	}

	scaled := make([]*profile.Profile, 0, numprofs)
	for _, p := range profs {
		if p != nil {
			scaled = append(scaled, p)
		}
	}

	// Merge profiles.
	if err := measurement.ScaleProfiles(scaled); err != nil {
		return nil, nil, false, err
	}

	p, err := profile.Merge(scaled)
	if err != nil {
		return nil, nil, false, err
	}

	// Combine mapping sources.
	msrc := make(plugin.MappingSources)
	for _, ms := range msrcs {
		for m, s := range ms {
			msrc[m] = append(msrc[m], s...)
		}
	}

	return p, msrc, save, nil
}

// setTmpDir sets the PPROF_TMPDIR environment variable with a new
// temp directory, if not already set.
func setTmpDir(ui plugin.UI) error {
	if profileDir := os.Getenv("PPROF_TMPDIR"); profileDir != "" {
		return nil
	}
	for _, tmpDir := range []string{os.Getenv("HOME") + "/pprof", "/tmp"} {
		if err := os.MkdirAll(tmpDir, 0755); err != nil {
			ui.PrintErr("Could not use temp dir ", tmpDir, ": ", err.Error())
			continue
		}
		os.Setenv("PPROF_TMPDIR", tmpDir)
		return nil
	}
	return fmt.Errorf("failed to identify temp dir")
}

// grabProfile fetches a profile. Returns the profile, sources for the
// profile mappings, a bool indicating if the profile was fetched
// remotely, and an error.
func grabProfile(s *source, source string, scale float64, fetcher plugin.Fetcher, obj plugin.ObjTool, ui plugin.UI) (p *profile.Profile, msrc plugin.MappingSources, remote bool, err error) {
	var src string
	duration, timeout := time.Duration(s.Seconds)*time.Second, time.Duration(s.Timeout)*time.Second
	if fetcher != nil {
		p, src, err = fetcher.Fetch(source, duration, timeout)
		if err != nil {
			return
		}
	}
	if err != nil || p == nil {
		// Fetch the profile over HTTP or from a file.
		p, src, err = fetch(source, duration, timeout, ui)
		if err != nil {
			return
		}
	}

	if err = p.CheckValid(); err != nil {
		return
	}

	// Apply local changes to the profile.
	p.Scale(scale)

	// Update the binary locations from command line and paths.
	locateBinaries(p, s, obj, ui)

	// Collect the source URL for all mappings.
	if src != "" {
		msrc = collectMappingSources(p, src)
		remote = true
	}
	return
}

// collectMappingSources saves the mapping sources of a profile.
func collectMappingSources(p *profile.Profile, source string) plugin.MappingSources {
	ms := plugin.MappingSources{}
	for _, m := range p.Mapping {
		src := struct {
			Source string
			Start  uint64
		}{
			source, m.Start,
		}
		if key := m.BuildID; key != "" {
			ms[key] = append(ms[key], src)
		}
		if key := m.File; key != "" {
			ms[key] = append(ms[key], src)
		}
	}
	return ms
}

// locateBinaries searches for binary files listed in the profile and, if found,
// updates the profile accordingly.
func locateBinaries(p *profile.Profile, s *source, obj plugin.ObjTool, ui plugin.UI) {
	// Construct search path to examine
	searchPath := os.Getenv("PPROF_BINARY_PATH")
	if searchPath == "" {
		// Use $HOME/pprof/binaries as default directory for local symbolization binaries
		searchPath = filepath.Join(os.Getenv("HOME"), "pprof", "binaries")
	}

mapping:
	for i, m := range p.Mapping {
		var baseName string
		// Replace executable filename/buildID with the overrides from source.
		// Assumes the executable is the first Mapping entry.
		if i == 0 {
			if s.ExecName != "" {
				m.File = s.ExecName
			}
			if s.BuildID != "" {
				m.BuildID = s.BuildID
			}
		}
		if m.File != "" {
			baseName = filepath.Base(m.File)
		}

		for _, path := range filepath.SplitList(searchPath) {
			var fileNames []string
			if m.BuildID != "" {
				fileNames = []string{filepath.Join(path, m.BuildID, baseName)}
				if matches, err := filepath.Glob(filepath.Join(path, m.BuildID, "*")); err == nil {
					fileNames = append(fileNames, matches...)
				}
			}
			if baseName != "" {
				fileNames = append(fileNames, filepath.Join(path, baseName))
			}
			for _, name := range fileNames {
				if f, err := obj.Open(name, m.Start, m.Limit, m.Offset); err == nil {
					defer f.Close()
					fileBuildID := f.BuildID()
					if m.BuildID != "" && m.BuildID != fileBuildID {
						ui.PrintErr("Ignoring local file " + name + ": build-id mismatch (" + m.BuildID + " != " + fileBuildID + ")")
					} else {
						m.File = name
						continue mapping
					}
				}
			}
		}
	}
}

// fetch fetches a profile from source, within the timeout specified,
// producing messages through the ui. It returns the profile and the
// url of the actual source of the profile for remote profiles.
func fetch(source string, duration, timeout time.Duration, ui plugin.UI) (p *profile.Profile, src string, err error) {
	var f io.ReadCloser

	if sourceURL, timeout := adjustURL(source, duration, timeout); sourceURL != "" {
		ui.Print("Fetching profile over HTTP from " + sourceURL)
		if duration > 0 {
			ui.Print(fmt.Sprintf("Please wait... (%v)", duration))
		}
		f, err = fetchURL(sourceURL, timeout)
		src = sourceURL
	} else {
		f, err = os.Open(source)
	}
	if err == nil {
		defer f.Close()
		p, err = profile.Parse(f)
	}
	return
}

// fetchURL fetches a profile from a URL using HTTP.
func fetchURL(source string, timeout time.Duration) (io.ReadCloser, error) {
	resp, err := httpGet(source, timeout)
	if err != nil {
		return nil, fmt.Errorf("http fetch %s: %v", source, err)
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("server response: %s", resp.Status)
	}

	return resp.Body, nil
}

// adjustURL validates if a profile source is a URL and returns an
// cleaned up URL and the timeout to use for retrieval over HTTP.
// If the source cannot be recognized as a URL it returns an empty string.
func adjustURL(source string, duration, timeout time.Duration) (string, time.Duration) {
	u, err := url.Parse(source)
	if err != nil || (u.Host == "" && u.Scheme != "" && u.Scheme != "file") {
		// Try adding http:// to catch sources of the form hostname:port/path.
		// url.Parse treats "hostname" as the scheme.
		u, err = url.Parse("http://" + source)
	}
	if err != nil || u.Host == "" {
		return "", 0
	}

	// Apply duration/timeout overrides to URL.
	values := u.Query()
	if duration > 0 {
		values.Set("seconds", fmt.Sprint(int(duration.Seconds())))
	} else {
		if urlSeconds := values.Get("seconds"); urlSeconds != "" {
			if us, err := strconv.ParseInt(urlSeconds, 10, 32); err == nil {
				duration = time.Duration(us) * time.Second
			}
		}
	}
	if timeout <= 0 {
		if duration > 0 {
			timeout = duration + duration/2
		} else {
			timeout = 60 * time.Second
		}
	}
	u.RawQuery = values.Encode()
	return u.String(), timeout
}

// httpGet is a wrapper around http.Get; it is defined as a variable
// so it can be redefined during for testing.
var httpGet = func(url string, timeout time.Duration) (*http.Response, error) {
	client := &http.Client{
		Transport: &http.Transport{
			ResponseHeaderTimeout: timeout + 5*time.Second,
		},
	}
	return client.Get(url)
}
