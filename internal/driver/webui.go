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
	"strconv"
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
	Total    int64
	Legend   []string
	Help     map[string]string
	Nodes    []string
	HTMLBody template.HTML
	TextBody string
	Top      []report.TextItem
}

func serveWebInterface(hostport string, p *profile.Profile, o *plugin.Options, wantBrowser bool) error {
	host, portStr, err := net.SplitHostPort(hostport)
	if err != nil {
		return fmt.Errorf("could not split http address: %v", err)
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		return fmt.Errorf("invalid port number: %v", err)
	}
	if host == "" {
		host = "localhost"
	}

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

	server := o.HTTPServer
	if server == nil {
		server = defaultWebServer
	}
	args := &plugin.HTTPServerArgs{
		Hostport: net.JoinHostPort(host, portStr),
		Host:     host,
		Port:     port,
		Handlers: map[string]http.Handler{
			"/":       http.HandlerFunc(ui.dot),
			"/top":    http.HandlerFunc(ui.top),
			"/disasm": http.HandlerFunc(ui.disasm),
			"/source": http.HandlerFunc(ui.source),
			"/peek":   http.HandlerFunc(ui.peek),
		},
	}

	if wantBrowser {
		go openBrowser("http://"+args.Hostport, o)
	}
	return server(args)
}

func defaultWebServer(args *plugin.HTTPServerArgs) error {
	ln, err := net.Listen("tcp", args.Hostport)
	if err != nil {
		return err
	}
	isLocal := isLocalhost(args.Host)
	handler := http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		if isLocal {
			// Only allow local clients
			host, _, err := net.SplitHostPort(req.RemoteAddr)
			if err != nil || !isLocalhost(host) {
				http.Error(w, "permission denied", http.StatusForbidden)
				return
			}
		}
		h := args.Handlers[req.URL.Path]
		if h == nil {
			// Fall back to default behavior
			h = http.DefaultServeMux
		}
		h.ServeHTTP(w, req)
	})
	s := &http.Server{Handler: handler}
	return s.Serve(ln)
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
	rpt *report.Report, errList, legend []string, data webArgs) {
	file := getFromLegend(legend, "File: ", "unknown")
	profile := getFromLegend(legend, "Type: ", "unknown")
	data.BaseURL = baseURL
	data.Title = file + " " + profile
	data.Errors = errList
	data.Total = rpt.Total()
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

	ui.render(w, "/", "graph", rpt, errList, legend, webArgs{
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

	ui.render(w, "/top", "top", rpt, errList, legend, webArgs{
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
	ui.render(w, "/disasm", "plaintext", rpt, errList, legend, webArgs{
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
	ui.render(w, "/source", "sourcelisting", rpt, errList, legend, webArgs{
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
	ui.render(w, "/peek", "plaintext", rpt, errList, legend, webArgs{
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
