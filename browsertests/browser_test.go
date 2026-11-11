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

package browsertests

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

func TestTopTable(t *testing.T) {
	maybeSkipBrowserTest(t)

	prof := makeFakeProfile()
	server := makeTestServer(t, prof)
	ctx := newContext(context.Background(), t)

	err := chromedp.Do(ctx,
		chromedp.Navigate(server.URL+"/ui/top"),
		chromedp.WaitVisible(chromedp.ID(`#toptable`)),

		// Check that fake profile entries show up in the right order.
		matchRegexp(t, "#node0", `200ms.*F2`),
		matchInOrder(t, "#toptable", "F2", "F3", "F1"),

		// Check sorting by cumulative count.
		chromedp.Click(chromedp.ID(`#cumhdr1`)),
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
	ctx := newContext(context.Background(), t)

	err := chromedp.Do(ctx,
		chromedp.Navigate(server.URL),
		chromedp.Evaluate[chromedp.Void](jsTestFixture),
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
	ctx := newContext(context.Background(), t)

	err := chromedp.Do(ctx,
		chromedp.Navigate(server.URL+"/ui/source?f=F3"),
		chromedp.WaitVisible(chromedp.ID(`#content`)),
		matchRegexp(t, "#content", `F3`),            // Header
		matchRegexp(t, "#content", `Total:.*100ms`), // Total for function
		matchRegexp(t, "#content", `\b22\b.*100ms`), // Line 22

		// Inline assembly starts hidden, toggles visible when the live source line
		// is clicked, and hides again on a second click.
		chromedp.WaitNotVisible(chromedp.CSS(`.asm`)),
		chromedp.Click(chromedp.CSS(`.livesrc`)),
		chromedp.WaitVisible(chromedp.CSS(`.asm`)),
		matchRegexp(t, ".asm", `d3:asm`),
		chromedp.Click(chromedp.CSS(`.livesrc`)),
		chromedp.WaitNotVisible(chromedp.CSS(`.asm`)),
	)
	if err != nil {
		t.Fatal(err)
	}
}

func TestGraph(t *testing.T) {
	maybeSkipBrowserTest(t)

	prof := makeFakeProfile()
	server := makeTestServer(t, prof)
	ctx := newContext(context.Background(), t)

	err := chromedp.Do(ctx,
		chromedp.Navigate(server.URL+"/ui/graph"),
		chromedp.WaitVisible(chromedp.ID(`#graph0`)),
		matchRegexp(t, "#graph", `F1`),
		matchRegexp(t, "#graph", `F2`),
		matchRegexp(t, "#graph", `F3`),

		// Clicking a graph node highlights its polygon and enables Refine links.
		clickElementCenter(`#node1 polygon`),
		chromedp.Poll[chromedp.Void](`document.querySelector('#node1 polygon').style.fill !== ''`),
		chromedp.Evaluate[chromedp.Void](`
			document.getElementById('focus').dispatchEvent(new Event('mouseenter'));
		`),
		chromedp.Poll[chromedp.Void](`new URL(document.getElementById('focus').href).searchParams.get('f') === '^F2$'`),

		// Clicking the selected node again unselects it.
		clickElementCenter(`#node1 polygon`),
		chromedp.Poll[chromedp.Void](`document.querySelector('#node1 polygon').style.fill === ''`),

		// Entering a search regex selects matching nodes and updates Refine links.
		chromedp.SendKeys(chromedp.ID(`#search`), "F3"),
		chromedp.Poll[chromedp.Void](`Array.from(document.querySelectorAll('#graph0 .node polygon')).filter(p => p.style.fill !== '').length === 1`),
		chromedp.Evaluate[chromedp.Void](`
			document.getElementById('focus').dispatchEvent(new Event('mouseenter'));
		`),
		chromedp.Poll[chromedp.Void](`new URL(document.getElementById('focus').href).searchParams.get('f') === 'F3'`),
	)
	if err != nil {
		t.Fatal(err)
	}
}

func TestConfigSaveDelete(t *testing.T) {
	maybeSkipBrowserTest(t)

	dir := t.TempDir()
	for _, env := range []string{"HOME", "XDG_CONFIG_HOME", "AppData"} {
		t.Setenv(env, dir)
	}

	prof := makeFakeProfile()
	server := makeTestServer(t, prof)
	ctx := newContext(context.Background(), t)

	err := chromedp.Do(ctx,
		chromedp.Navigate(server.URL+"/ui/top?f=F2"),
		chromedp.WaitVisible(chromedp.ID(`#toptable`)),

		// Open Config -> Save as..., save as "mycfg" (triggers POST /ui/saveconfig
		// and reloads the page).
		chromedp.Evaluate[chromedp.Void](`document.getElementById('save-config').click()`),
		chromedp.WaitVisible(chromedp.ID(`#save-dialog`)),
		chromedp.SendKeys(chromedp.ID(`#save-name`), "mycfg"),
		chromedp.Click(chromedp.ID(`#save-confirm`)),
		chromedp.WaitReady(chromedp.CSS(`.menu-delete-btn[data-config="mycfg"]`)),

		// Delete "mycfg" via the menu delete button and confirm dialog (triggers
		// DELETE /ui/deleteconfig).
		chromedp.Evaluate[chromedp.Void](`
			document.querySelector('.menu-delete-btn[data-config="mycfg"]').click();
		`),
		chromedp.WaitVisible(chromedp.ID(`#delete-dialog`)),
		chromedp.Click(chromedp.ID(`#delete-confirm`)),
		chromedp.WaitNotPresent(chromedp.CSS(`.menu-delete-btn[data-config="mycfg"]`)),
	)
	if err != nil {
		t.Fatal(err)
	}
}

func TestProfileDetails(t *testing.T) {
	maybeSkipBrowserTest(t)

	prof := makeFakeProfile()
	prof.Comments = []string{"RSS is unavailable on this platform"}
	server := makeTestServer(t, prof)
	ctx := newContext(context.Background(), t)
	err := chromedp.Do(ctx,
		chromedp.Navigate(server.URL+"/top"),
		chromedp.WaitVisible(chromedp.ID(`#details`)),

		// Space and Enter do not open profile information without button focus.
		chromedp.KeyEvent(" "),
		chromedp.WaitNotVisible(chromedp.ID(`#detailsbox`)),
		chromedp.KeyEvent("\r"),
		chromedp.WaitNotVisible(chromedp.ID(`#detailsbox`)),
		chromedp.Click(chromedp.ID(`#details`)),
		chromedp.WaitVisible(chromedp.ID(`#detailsbox`)),
		matchRegexp(t, "#detailsbox", "RSS is unavailable on this platform"),

		// Keyboard activation works when the button has focus.
		chromedp.Focus(chromedp.ID(`#details`)),
		chromedp.KeyEvent(" "),
		chromedp.WaitNotVisible(chromedp.ID(`#detailsbox`)),
		chromedp.KeyEvent("\r"),
		chromedp.WaitVisible(chromedp.ID(`#detailsbox`)),
	)
	if err != nil {
		t.Fatal(err)
	}
}

func newContext(ctx context.Context, t *testing.T) context.Context {
	opts := append(chromedp.DefaultExecAllocatorOptions[:],
		// Ubuntu 23+ enables AppArmor in a way that conflicts with Chrome's usage
		// of unprivileged user namespaces as part of the sandboxing. Since our
		// test does not visit any external websites, we don't really need the
		// sandbox, so disable it.
		chromedp.NoSandbox,
		// Ensure desktop width (>= 800px) so .header input (#search) is visible.
		chromedp.WindowSize(1280, 800),
	)

	// browserDeadline is the deadline to use for browser tests. This is long to
	// reduce flakiness in CI workflows.
	const browserDeadline = time.Second * 90

	ctx, cancel := chromedp.NewExecAllocator(ctx, opts...)
	t.Cleanup(cancel)
	ctx, cancel = context.WithTimeout(ctx, browserDeadline)
	t.Cleanup(cancel)
	ctx, cancel = chromedp.NewContext(ctx)
	console := chromedp.Console(ctx)
	done := make(chan struct{})
	go func() {
		defer close(done)
		for msg, err := range console {
			if err != nil {
				return
			}
			if msg.Type == chromedp.ConsoleError || msg.IsException() {
				// Ignore Chrome's routine 404 when probing /favicon.ico.
				if msg.Source == "network" && strings.Contains(msg.URL, "favicon.ico") {
					continue
				}
				t.Errorf("browser console %s: %s (source=%s url=%s)", msg.Type, msg.Text, msg.Source, msg.URL)
			}
		}
	}()
	t.Cleanup(func() {
		cancel()
		<-done
	})
	return ctx
}

// clickElementCenter clicks the center of the first element matching query
// using bounding-client coordinates (which works on transformed SVG shapes
// where DOM.getBoxModel does not).
func clickElementCenter(query string) chromedp.Action[chromedp.Void] {
	return chromedp.Func(func(ctx context.Context, target *chromedp.Target) error {
		js := fmt.Sprintf(`(() => {
			const r = document.querySelector(%q).getBoundingClientRect();
			return [r.left + r.width / 2, r.top + r.height / 2];
		})()`, query)
		coords, err := chromedp.Evaluate[[]float64](js)(ctx, target)
		if err != nil {
			return err
		}
		_, err = chromedp.MouseClickXY(coords[0], coords[1])(ctx, target)
		return err
	})
}

// matchRegexp is a chromedp.Action that fetches the text of the first
// node that matched query and checks that the text matches regexp re.
func matchRegexp(t *testing.T, query, re string) chromedp.Action[chromedp.Void] {
	return chromedp.Func(func(ctx context.Context, target *chromedp.Target) error {
		value, err := chromedp.Text(chromedp.CSS(query))(ctx, target)
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
	})
}

// matchInOrder is a chromedp.Action that fetches the text of the first
// node that matched query and checks that the supplied sequence of
// strings occur in order in the text.
func matchInOrder(t *testing.T, query string, sequence ...string) chromedp.Action[chromedp.Void] {
	return chromedp.Func(func(ctx context.Context, target *chromedp.Target) error {
		value, err := chromedp.Text(chromedp.CSS(query))(ctx, target)
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
	})
}

// eval runs the specified javascript in the browser. The javascript must
// return an [][]any, where each of the []any starts with either "LOG" or
// "ERROR" (see testdata/testfixture.js).
func eval(t *testing.T, js string) chromedp.Action[chromedp.Void] {
	return chromedp.Func(func(ctx context.Context, target *chromedp.Target) error {
		result, err := chromedp.Evaluate[[][]any](js)(ctx, target)
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
	})
}

//go:embed testdata/testfixture.js
var jsTestFixture string
