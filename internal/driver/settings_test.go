package driver

import (
	"io/ioutil"
	"net/url"
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

// settingsDirAndFile returns a directory in which settings should be stored
// and the name of the settings file. The caller must delete the directory when
// done.
func settingsDirAndFile(t *testing.T) (string, string) {
	tmpDir, err := ioutil.TempDir("", "pprof_settings_test")
	if err != nil {
		t.Fatalf("error creating temporary directory: %v", err)
	}
	return tmpDir, filepath.Join(tmpDir, "settings.json")
}

func TestSettings(t *testing.T) {
	tmpDir, fname := settingsDirAndFile(t)
	defer os.RemoveAll(tmpDir)
	s, err := readSettings(fname)
	if err != nil {
		t.Fatalf("error reading empty settings: %v", err)
	}
	if len(s.Configs) != 0 {
		t.Fatalf("expected empty settings; got %v", s)
	}
	s.Configs = append(s.Configs, namedConfig{
		Name: "Foo",
		config: config{
			Focus: "focus",
			// Ensure that transient fields are not saved/restored.
			Output:     "output",
			SourcePath: "source",
			TrimPath:   "trim",
			DivideBy:   -2,
		},
	})
	if err := writeSettings(fname, s); err != nil {
		t.Fatal(err)
	}
	s2, err := readSettings(fname)
	if err != nil {
		t.Fatal(err)
	}

	// Change the transient fields to their expected values.
	s.Configs[0].resetTransient()
	if !reflect.DeepEqual(s, s2) {
		t.Fatalf("ReadSettings = %v; expected %v", s2, s)
	}
}

func TestParseConfig(t *testing.T) {
	// Use all the fields to check they are saved/restored from URL.
	cfg := config{
		Output:              "",
		DropNegative:        true,
		CallTree:            true,
		RelativePercentages: true,
		Unit:                "auto",
		CompactLabels:       true,
		SourcePath:          "",
		TrimPath:            "",
		NodeCount:           10,
		NodeFraction:        0.1,
		EdgeFraction:        0.2,
		Trim:                true,
		Focus:               "focus",
		Ignore:              "ignore",
		PruneFrom:           "prune_from",
		Hide:                "hide",
		Show:                "show",
		ShowFrom:            "show_from",
		TagFocus:            "tagfocus",
		TagIgnore:           "tagignore",
		TagShow:             "tagshow",
		TagHide:             "taghide",
		DivideBy:            1,
		Mean:                true,
		Normalize:           true,
		Sort:                "cum",
		Granularity:         "functions",
		NoInlines:           true,
	}
	url, changed := cfg.makeURL(url.URL{})
	if !changed {
		t.Error("applyConfig returned changed=false after applying non-empty config")
	}
	cfg2 := defaultConfig()
	if err := cfg2.applyURL(url.Query()); err != nil {
		t.Fatalf("fromURL failed: %v", err)
	}
	if !reflect.DeepEqual(cfg, cfg2) {
		t.Fatalf("parsed config = %+v; expected match with %+v", cfg2, cfg)
	}
	if url2, changed := cfg.makeURL(url); changed {
		t.Errorf("ApplyConfig returned changed=true after applying same config (%q instead of expected %q", url2.String(), url.String())
	}
}

// TestDefaultConfig verifies that default config values are omitted from URL.
func TestDefaultConfig(t *testing.T) {
	cfg := defaultConfig()
	url, changed := cfg.makeURL(url.URL{})
	if changed {
		t.Error("applyConfig returned changed=true after applying default config")
	}
	if url.String() != "" {
		t.Errorf("applyConfig returned %q; expecting %q", url.String(), "")
	}
}

func TestConfigMenu(t *testing.T) {
	// Save some test settings.
	tmpDir, fname := settingsDirAndFile(t)
	defer os.RemoveAll(tmpDir)
	a, b := defaultConfig(), defaultConfig()
	a.Focus, b.Focus = "foo", "bar"
	s := &settings{
		Configs: []namedConfig{
			{Name: "A", config: a},
			{Name: "B", config: b},
		},
	}
	if err := writeSettings(fname, s); err != nil {
		t.Fatal("error writing settings", err)
	}

	pageURL, _ := url.Parse("/top?f=foo")
	menu := configMenu(fname, *pageURL)
	want := []configMenuEntry{
		{Name: "Default", URL: "/top", Current: false, UserConfig: false},
		{Name: "A", URL: "/top?f=foo", Current: true, UserConfig: true},
		{Name: "B", URL: "/top?f=bar", Current: false, UserConfig: true},
	}
	if !reflect.DeepEqual(menu, want) {
		t.Errorf("ConfigMenu returned %v; want %v", menu, want)
	}
}

func TestEditConfig(t *testing.T) {
	tmpDir, fname := settingsDirAndFile(t)
	defer os.RemoveAll(tmpDir)

	type testConfig struct {
		name  string
		focus string
		hide  string
	}
	type testCase struct {
		remove  bool
		request string
		expect  []testConfig
	}
	for _, c := range []testCase{
		// Create setting c1
		{false, "/?config=c1&f=foo", []testConfig{
			{"c1", "foo", ""},
		}},
		// Create setting c2
		{false, "/?config=c2&h=bar", []testConfig{
			{"c1", "foo", ""},
			{"c2", "", "bar"},
		}},
		// Overwrite c1
		{false, "/?config=c1&f=baz", []testConfig{
			{"c1", "baz", ""},
			{"c2", "", "bar"},
		}},
		// Delete c2
		{true, "c2", []testConfig{
			{"c1", "baz", ""},
		}},
	} {
		if c.remove {
			if err := removeConfig(fname, c.request); err != nil {
				t.Errorf("error removing config %s: %v", c.request, err)
				continue
			}
		} else {
			req, err := url.Parse(c.request)
			if err != nil {
				t.Errorf("error parsing request %q: %v", c.request, err)
				continue
			}
			if err := setConfig(fname, *req); err != nil {
				t.Errorf("error saving request %q: %v", c.request, err)
				continue
			}
		}

		// Check resulting settings.
		s, err := readSettings(fname)
		if err != nil {
			t.Errorf("error reading settings after applying %q: %v", c.request, err)
			continue
		}
		// Convert to a list that can be compared to c.expect
		got := make([]testConfig, len(s.Configs))
		for i, c := range s.Configs {
			got[i] = testConfig{c.Name, c.Focus, c.Hide}
		}
		if !reflect.DeepEqual(got, c.expect) {
			t.Errorf("Settings after applying %q = %v; want %v", c.request, got, c.expect)
		}
	}
}

func TestAssign(t *testing.T) {
	baseConfig := currentConfig()
	defer setCurrentConfig(baseConfig)

	// Test assigning to a simple field.
	if err := configure("nodecount", "20"); err != nil {
		t.Errorf("error setting nodecount: %v", err)
	}
	if n := currentConfig().NodeCount; n != 20 {
		t.Errorf("incorrect nodecount; expecting 20, got %d", n)
	}

	// Test assignment to a group field.
	if err := configure("granularity", "files"); err != nil {
		t.Errorf("error setting granularity: %v", err)
	}
	if g := currentConfig().Granularity; g != "files" {
		t.Errorf("incorrect granularity; expecting %v, got %v", "files", g)
	}

	// Test assignment to one choice of a group field.
	if err := configure("lines", "t"); err != nil {
		t.Errorf("error setting lines: %v", err)
	}
	if g := currentConfig().Granularity; g != "lines" {
		t.Errorf("incorrect granularity; expecting %v, got %v", "lines", g)
	}

	// Test assignment to invalid choice,
	if err := configure("granularity", "cheese"); err == nil {
		t.Errorf("allowed assignment of invalid granularity")
	}
}
