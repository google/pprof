// Copyright 2023 Google Inc. All Rights Reserved.
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
	"context"
	"fmt"
	"os/exec"
	"regexp"
	"runtime"
	"strings"
	"testing"
	"time"

	_ "embed"

	"github.com/chromedp/chromedp"
)

func maybeSkipBrowserTest(t *testing.T) {
	// Limit to just Linux for now since this is expensive and the
	// browser interactions should be platform agnostic.  If we ever
	// see a benefit from wider testing, we can relax this.
	if runtime.GOOS != "linux" || runtime.GOARCH != "amd64" {
		t.Skip("This test only works on x86-64 Linux")
	}

	// Check that browser is available.
	if _, err := exec.LookPath("google-chrome"); err == nil {
		return
	}
	if _, err := exec.LookPath("chrome"); err == nil {
		return
	}
	t.Skip("chrome not available")
}

// browserDeadline is the deadline to use for browser tests. This is long to
// reduce flakiness in CI workflows.
const browserDeadline = time.Second * 60

func TestTopTable(t *testing.T) {
	maybeSkipBrowserTest(t)

	prof := makeFakeProfile()
	server := makeTestServer(t, prof)
	ctx, cancel := context.WithTimeout(context.Background(), browserDeadline)
	defer cancel()
	ctx, cancel = chromedp.NewContext(ctx)
	defer cancel()

	err := chromedp.Run(ctx,
		chromedp.Navigate(server.URL+"/top"),
		chromedp.WaitVisible(`#toptable`, chromedp.ByID),

		// Check that fake profile entries show up in the right order.
		matchRegexp(t, "#node0", `200ms.*F2`),
		matchInOrder(t, "#toptable", "F2", "F3", "F1"),

		// Check sorting by cumulative count.
		chromedp.Click(`#cumhdr1`, chromedp.ByID),
		matchInOrder(t, "#toptable", "F1", "F2", "F3"),
	)
	if err != nil {
		t.Fatal(err)
	}
}

func TestFlameGraph(t *testing.T) {
	maybeSkipBrowserTest(t)

	prof := makeFakeProfile()
	server := makeTestServer(t, prof)
	ctx, cancel := context.WithTimeout(context.Background(), browserDeadline)
	defer cancel()
	ctx, cancel = chromedp.NewContext(ctx)
	defer cancel()

	err := chromedp.Run(ctx,
		chromedp.Navigate(server.URL+"/flamegraph"),
		chromedp.Evaluate(jsTestFixture, nil),
		eval(t, jsCheckFlame),
	)
	if err != nil {
		t.Fatal(err)
	}
}

//go:embed testdata/testflame.js
var jsCheckFlame string

func TestSource(t *testing.T) {
	maybeSkipBrowserTest(t)

	prof := makeFakeProfile()
	server := makeTestServer(t, prof)
	ctx, cancel := context.WithTimeout(context.Background(), browserDeadline)
	defer cancel()
	ctx, cancel = chromedp.NewContext(ctx)
	defer cancel()

	err := chromedp.Run(ctx,
		chromedp.Navigate(server.URL+"/source?f=F3"),
		chromedp.WaitVisible(`#content`, chromedp.ByID),
		matchRegexp(t, "#content", `F3`),            // Header
		matchRegexp(t, "#content", `Total:.*100ms`), // Total for function
		matchRegexp(t, "#content", `\b22\b.*100ms`), // Line 22
	)
	if err != nil {
		t.Fatal(err)
	}
}

// matchRegexp is a chromedp.Action that fetches the text of the first
// node that matched query and checks that the text matches regexp re.
func matchRegexp(t *testing.T, query, re string) chromedp.ActionFunc {
	return func(ctx context.Context) error {
		var value string
		err := chromedp.Text(query, &value, chromedp.ByQuery).Do(ctx)
		if err != nil {
			return fmt.Errorf("text %s: %v", query, err)
		}
		t.Logf("text %s:\n%s", query, value)
		m, err := regexp.MatchString(re, value)
		if err != nil {
			return err
		}
		if !m {
			return fmt.Errorf("%s: did not find %q in\n%s", query, re, value)
		}
		return nil
	}
}

// matchInOrder is a chromedp.Action that fetches the text of the first
// node that matched query and checks that the supplied sequence of
// strings occur in order in the text.
func matchInOrder(t *testing.T, query string, sequence ...string) chromedp.ActionFunc {
	return func(ctx context.Context) error {
		var value string
		err := chromedp.Text(query, &value, chromedp.ByQuery).Do(ctx)
		if err != nil {
			return fmt.Errorf("text %s: %v", query, err)
		}
		t.Logf("text %s:\n%s", query, value)
		remaining := value
		for _, s := range sequence {
			pos := strings.Index(remaining, s)
			if pos < 0 {
				return fmt.Errorf("%s: did not find %q in expected order %v  in\n%s", query, s, sequence, value)
			}
			remaining = remaining[pos+len(s):]
		}
		return nil
	}
}

// eval runs the specified javascript in the browser. The javascript must
// return an [][]any, where each of the []any starts with either "LOG" or
// "ERROR" (see testdata/testfixture.js).
func eval(t *testing.T, js string) chromedp.ActionFunc {
	return func(ctx context.Context) error {
		var result [][]any
		err := chromedp.Evaluate(js, &result).Do(ctx)
		if err != nil {
			return err
		}
		for _, s := range result {
			if len(s) > 0 && s[0] == "LOG" {
				t.Log(s[1:]...)
			} else if len(s) > 0 && s[0] == "ERROR" {
				t.Error(s[1:]...)
			} else {
				t.Error(s...) // Treat missing prefix as an error.
			}
		}
		return nil
	}
}

//go:embed testdata/testfixture.js
var jsTestFixture string
