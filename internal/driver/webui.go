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
	"net"
	"net/http"
	gourl "net/url"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/google/pprof/internal/graph"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/report"
	"github.com/google/pprof/profile"
)

// webInterface holds the state needed for serving a browser based interface.
type webInterface struct {
	prof      *profile.Profile
	options   *plugin.Options
	help      map[string]string
	templates *template.Template
}

func makeWebInterface(p *profile.Profile, opt *plugin.Options) *webInterface {
	templates := template.New("templategroup")
	addTemplates(templates)
	report.AddSourceTemplates(templates)
	return &webInterface{
		prof:      p,
		options:   opt,
		help:      make(map[string]string),
		templates: templates,
	}
}

// maxEntries is the maximum number of entries to print for text interfaces.
const maxEntries = 50

// errorCatcher is a UI that captures errors for reporting to the browser.
type errorCatcher struct {
	plugin.UI
	errors []string
}

func (ec *errorCatcher) PrintErr(args ...interface{}) {
	ec.errors = append(ec.errors, strings.TrimSuffix(fmt.Sprintln(args...), "\n"))
	ec.UI.PrintErr(args...)
}

// webArgs contains arguments passed to templates in webhtml.go.
type webArgs struct {
	BaseURL  string
	Title    string
	Errors   []string
	Legend   []string
	Help     map[string]string
	Nodes    []string
	HTMLBody template.HTML
	TextBody string
	Top      []report.TextItem
}

func serveWebInterface(hostport string, p *profile.Profile, o *plugin.Options) error {
	interactiveMode = true
	ui := makeWebInterface(p, o)
	for n, c := range pprofCommands {
		ui.help[n] = c.description
	}
	for n, v := range pprofVariables {
		ui.help[n] = v.help
	}
	ui.help["details"] = "Show information about the profile and this view"
	ui.help["graph"] = "Display profile as a directed graph"
	ui.help["reset"] = "Show the entire profile"

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
	mux.Handle("/top", wrap(http.HandlerFunc(ui.top)))
	mux.Handle("/disasm", wrap(http.HandlerFunc(ui.disasm)))
	mux.Handle("/source", wrap(http.HandlerFunc(ui.source)))
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

func varsFromURL(u *gourl.URL) variables {
	vars := pprofVariables.makeCopy()
	vars["focus"].value = u.Query().Get("f")
	vars["show"].value = u.Query().Get("s")
	vars["ignore"].value = u.Query().Get("i")
	vars["hide"].value = u.Query().Get("h")
	return vars
}

// makeReport generates a report for the specified command.
func (ui *webInterface) makeReport(w http.ResponseWriter, req *http.Request,
	cmd []string, vars ...string) (*report.Report, []string) {
	v := varsFromURL(req.URL)
	for i := 0; i+1 < len(vars); i += 2 {
		v[vars[i]].value = vars[i+1]
	}
	catcher := &errorCatcher{UI: ui.options.UI}
	options := *ui.options
	options.UI = catcher
	_, rpt, err := generateRawReport(ui.prof, cmd, v, &options)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return nil, nil
	}
	return rpt, catcher.errors
}

// render generates html using the named template based on the contents of data.
func (ui *webInterface) render(w http.ResponseWriter, baseURL, tmpl string,
	errList, legend []string, data webArgs) {
	file := getFromLegend(legend, "File: ", "unknown")
	profile := getFromLegend(legend, "Type: ", "unknown")
	data.BaseURL = baseURL
	data.Title = file + " " + profile
	data.Errors = errList
	data.Legend = legend
	data.Help = ui.help
	html := &bytes.Buffer{}
	if err := ui.templates.ExecuteTemplate(html, tmpl, data); err != nil {
		http.Error(w, "internal template error", http.StatusInternalServerError)
		ui.options.UI.PrintErr(err)
		return
	}
	w.Header().Set("Content-Type", "text/html")
	w.Write(html.Bytes())
}

// dot generates a web page containing an svg diagram.
func (ui *webInterface) dot(w http.ResponseWriter, req *http.Request) {
	// Disable prefix matching behavior of net/http.
	if req.URL.Path != "/" {
		http.NotFound(w, req)
		return
	}

	rpt, errList := ui.makeReport(w, req, []string{"svg"})
	if rpt == nil {
		return // error already reported
	}

	// Generate dot graph.
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

	// Get all node names into an array.
	nodes := []string{""} // dot starts with node numbered 1
	for _, n := range g.Nodes {
		nodes = append(nodes, n.Info.Name)
	}

	ui.render(w, "/", "graph", errList, legend, webArgs{
		HTMLBody: template.HTML(string(svg)),
		Nodes:    nodes,
	})
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

func (ui *webInterface) top(w http.ResponseWriter, req *http.Request) {
	rpt, errList := ui.makeReport(w, req, []string{"top"}, "nodecount", "500")
	if rpt == nil {
		return // error already reported
	}
	top, legend := report.TextItems(rpt)
	var nodes []string
	for _, item := range top {
		nodes = append(nodes, item.Name)
	}

	ui.render(w, "/top", "top", errList, legend, webArgs{
		Top:   top,
		Nodes: nodes,
	})
}

// disasm generates a web page containing disassembly.
func (ui *webInterface) disasm(w http.ResponseWriter, req *http.Request) {
	args := []string{"disasm", req.URL.Query().Get("f")}
	rpt, errList := ui.makeReport(w, req, args)
	if rpt == nil {
		return // error already reported
	}

	out := &bytes.Buffer{}
	if err := report.PrintAssembly(out, rpt, ui.options.Obj, maxEntries); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}

	legend := report.ProfileLabels(rpt)
	ui.render(w, "/disasm", "plaintext", errList, legend, webArgs{
		TextBody: out.String(),
	})

}

// source generates a web page containing source code annotated with profile
// data.
func (ui *webInterface) source(w http.ResponseWriter, req *http.Request) {
	args := []string{"weblist", req.URL.Query().Get("f")}
	rpt, errList := ui.makeReport(w, req, args)
	if rpt == nil {
		return // error already reported
	}

	// Generate source listing.
	var body bytes.Buffer
	if err := report.PrintWebList(&body, rpt, ui.options.Obj, maxEntries); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}

	legend := report.ProfileLabels(rpt)
	ui.render(w, "/source", "sourcelisting", errList, legend, webArgs{
		HTMLBody: template.HTML(body.String()),
	})
}

// peek generates a web page listing callers/callers.
func (ui *webInterface) peek(w http.ResponseWriter, req *http.Request) {
	args := []string{"peek", req.URL.Query().Get("f")}
	rpt, errList := ui.makeReport(w, req, args, "lines", "t")
	if rpt == nil {
		return // error already reported
	}

	out := &bytes.Buffer{}
	if err := report.Generate(out, rpt, ui.options.Obj); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}

	legend := report.ProfileLabels(rpt)
	ui.render(w, "/peek", "plaintext", errList, legend, webArgs{
		TextBody: out.String(),
	})
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
