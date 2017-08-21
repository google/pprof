// Copyright 2017 Google Inc. All Rights Reserved.
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
	"bytes"
	"fmt"
	"html/template"
	"io"
	"net"
	"net/http"
	gourl "net/url"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"time"

	"github.com/google/pprof/internal/graph"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/report"
	"github.com/google/pprof/profile"
)

// webInterface holds the state needed for serving a browser based interface.
type webInterface struct {
	prof    *profile.Profile
	options *plugin.Options
	help    map[string]string
}

// errorCatcher is a UI that captures errors for reporting to the browser.
type errorCatcher struct {
	plugin.UI
	errors []string
}

func (ec *errorCatcher) PrintErr(args ...interface{}) {
	ec.errors = append(ec.errors, strings.TrimSuffix(fmt.Sprintln(args...), "\n"))
	ec.UI.PrintErr(args...)
}

func serveWebInterface(hostport string, p *profile.Profile, o *plugin.Options) error {
	interactiveMode = true
	ui := &webInterface{
		prof:    p,
		options: o,
		help:    make(map[string]string),
	}
	for n, c := range pprofCommands {
		ui.help[n] = c.description
	}
	for n, v := range pprofVariables {
		ui.help[n] = v.help
	}

	ln, url, isLocal, err := newListenerAndURL(hostport)
	if err != nil {
		return err
	}

	// authorization wrapper
	wrap := o.HTTPWrapper
	if wrap == nil {
		if isLocal {
			// Only allow requests from local host.
			wrap = checkLocalHost
		} else {
			wrap = func(h http.Handler) http.Handler { return h }
		}
	}

	mux := http.NewServeMux()
	mux.Handle("/", wrap(http.HandlerFunc(ui.dot)))
	mux.Handle("/disasm", wrap(http.HandlerFunc(ui.disasm)))
	mux.Handle("/weblist", wrap(http.HandlerFunc(ui.weblist)))
	mux.Handle("/peek", wrap(http.HandlerFunc(ui.peek)))

	s := &http.Server{Handler: mux}
	go openBrowser(url, o)
	return s.Serve(ln)
}

func newListenerAndURL(hostport string) (ln net.Listener, url string, isLocal bool, err error) {
	host, _, err := net.SplitHostPort(hostport)
	if err != nil {
		return nil, "", false, err
	}
	if host == "" {
		host = "localhost"
	}
	if ln, err = net.Listen("tcp", hostport); err != nil {
		return nil, "", false, err
	}
	url = fmt.Sprint("http://", net.JoinHostPort(host, fmt.Sprint(ln.Addr().(*net.TCPAddr).Port)))
	return ln, url, isLocalhost(host), nil
}

func isLocalhost(host string) bool {
	for _, v := range []string{"localhost", "127.0.0.1", "[::1]", "::1"} {
		if host == v {
			return true
		}
	}
	return false
}

func openBrowser(url string, o *plugin.Options) {
	// Construct URL.
	u, _ := gourl.Parse(url)
	q := u.Query()
	for _, p := range []struct{ param, key string }{
		{"f", "focus"},
		{"s", "show"},
		{"i", "ignore"},
		{"h", "hide"},
	} {
		if v := pprofVariables[p.key].value; v != "" {
			q.Set(p.param, v)
		}
	}
	u.RawQuery = q.Encode()

	// Give server a little time to get ready.
	time.Sleep(time.Millisecond * 500)

	for _, b := range browsers() {
		args := strings.Split(b, " ")
		if len(args) == 0 {
			continue
		}
		viewer := exec.Command(args[0], append(args[1:], u.String())...)
		viewer.Stderr = os.Stderr
		if err := viewer.Start(); err == nil {
			return
		}
	}
	// No visualizer succeeded, so just print URL.
	o.UI.PrintErr(u.String())
}

func checkLocalHost(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		host, _, err := net.SplitHostPort(req.RemoteAddr)
		if err != nil || !isLocalhost(host) {
			http.Error(w, "permission denied", http.StatusForbidden)
			return
		}
		h.ServeHTTP(w, req)
	})
}

// dot generates a web page containing an svg diagram.
func (ui *webInterface) dot(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path != "/" {
		http.NotFound(w, req)
		return
	}

	// Capture any error messages generated while generating a report.
	catcher := &errorCatcher{UI: ui.options.UI}
	options := *ui.options
	options.UI = catcher

	// Generate dot graph.
	args := []string{"svg"}
	vars := pprofVariables.makeCopy()
	vars["focus"].value = req.URL.Query().Get("f")
	vars["show"].value = req.URL.Query().Get("s")
	vars["ignore"].value = req.URL.Query().Get("i")
	vars["hide"].value = req.URL.Query().Get("h")
	_, rpt, err := generateRawReport(ui.prof, args, vars, &options)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}
	g, config := report.GetDOT(rpt)
	legend := config.Labels
	config.Labels = nil
	dot := &bytes.Buffer{}
	graph.ComposeDot(dot, g, &graph.DotAttributes{}, config)

	// Convert to svg.
	svg, err := dotToSvg(dot.Bytes())
	if err != nil {
		http.Error(w, "Could not execute dot; may need to install graphviz.",
			http.StatusNotImplemented)
		ui.options.UI.PrintErr("Failed to execute dot. Is Graphviz installed?\n", err)
		return
	}

	// Get regular expression for each node.
	nodes := []string{""}
	for _, n := range g.Nodes {
		nodes = append(nodes, regexp.QuoteMeta(n.Info.Name))
	}

	// Embed in html.
	file := getFromLegend(legend, "File: ", "unknown")
	profile := getFromLegend(legend, "Type: ", "unknown")
	data := struct {
		Title  string
		Errors []string
		Svg    template.HTML
		Legend []string
		Nodes  []string
		Help   map[string]string
	}{
		Title:  file + " " + profile,
		Errors: catcher.errors,
		Svg:    template.HTML(string(svg)),
		Legend: legend,
		Nodes:  nodes,
		Help:   ui.help,
	}
	html := &bytes.Buffer{}
	if err := graphTemplate.Execute(html, data); err != nil {
		http.Error(w, "internal template error", http.StatusInternalServerError)
		ui.options.UI.PrintErr(err)
		return
	}
	w.Header().Set("Content-Type", "text/html")
	w.Write(html.Bytes())
}

func dotToSvg(dot []byte) ([]byte, error) {
	cmd := exec.Command("dot", "-Tsvg")
	out := &bytes.Buffer{}
	cmd.Stdin, cmd.Stdout, cmd.Stderr = bytes.NewBuffer(dot), out, os.Stderr
	if err := cmd.Run(); err != nil {
		return nil, err
	}

	// Fix dot bug related to unquoted amperands.
	svg := bytes.Replace(out.Bytes(), []byte("&;"), []byte("&amp;;"), -1)

	// Cleanup for embedding by dropping stuff before the <svg> start.
	if pos := bytes.Index(svg, []byte("<svg")); pos >= 0 {
		svg = svg[pos:]
	}
	return svg, nil
}

// disasm generates a web page containing disassembly.
func (ui *webInterface) disasm(w http.ResponseWriter, req *http.Request) {
	ui.output(w, req, "disasm", "text/plain", pprofVariables.makeCopy())
}

// weblist generates a web page containing disassembly.
func (ui *webInterface) weblist(w http.ResponseWriter, req *http.Request) {
	ui.output(w, req, "weblist", "text/html", pprofVariables.makeCopy())
}

// peek generates a web page listing callers/callers.
func (ui *webInterface) peek(w http.ResponseWriter, req *http.Request) {
	vars := pprofVariables.makeCopy()
	vars.set("lines", "t") // Switch to line granularity
	ui.output(w, req, "peek", "text/plain", vars)
}

// output generates a webpage that contains the output of the specified pprof cmd.
func (ui *webInterface) output(w http.ResponseWriter, req *http.Request, cmd, ctype string, vars variables) {
	focus := req.URL.Query().Get("f")
	if focus == "" {
		fmt.Fprintln(w, "no argument supplied for "+cmd)
		return
	}

	// Capture any error messages generated while generating a report.
	catcher := &errorCatcher{UI: ui.options.UI}
	options := *ui.options
	options.UI = catcher

	args := []string{cmd, focus}
	_, rpt, err := generateRawReport(ui.prof, args, vars, &options)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}

	out := &bytes.Buffer{}
	if err := report.Generate(out, rpt, ui.options.Obj); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}

	if len(catcher.errors) > 0 {
		w.Header().Set("Content-Type", "text/plain")
		for _, msg := range catcher.errors {
			fmt.Println(w, msg)
		}
		return
	}

	w.Header().Set("Content-Type", ctype)
	io.Copy(w, out)
}

// getFromLegend returns the suffix of an entry in legend that starts
// with param.  It returns def if no such entry is found.
func getFromLegend(legend []string, param, def string) string {
	for _, s := range legend {
		if strings.HasPrefix(s, param) {
			return s[len(param):]
		}
	}
	return def
}
