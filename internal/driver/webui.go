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
	"os"
	"os/exec"
	"regexp"
	"strings"

	"github.com/google/pprof/internal/graph"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/internal/report"
	"github.com/google/pprof/profile"
)

// webUI holds the state needed for serving a browser based interface.
type webUI struct {
	prof    *profile.Profile
	options *plugin.Options
}

func serveWebInterface(port int, p *profile.Profile, o *plugin.Options) error {
	interactiveMode = true
	ui := &webUI{
		prof:    p,
		options: o,
	}
	// authorization wrapper
	wrap := o.HTTPWrapper
	if wrap == nil {
		// only allow requests from local host
		wrap = checkLocalHost
	}
	http.Handle("/", wrap(http.HandlerFunc(ui.dot)))
	http.Handle("/disasm", wrap(http.HandlerFunc(ui.disasm)))
	http.Handle("/weblist", wrap(http.HandlerFunc(ui.weblist)))
	return http.ListenAndServe(fmt.Sprint(":", port), nil)
}

func checkLocalHost(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		host, _, err := net.SplitHostPort(req.RemoteAddr)
		if err != nil || ((host != "127.0.0.1") && (host != "::1")) {
			http.Error(w, "permission denied", http.StatusForbidden)
			return
		}
		h.ServeHTTP(w, req)
	})
}

// disasm generates a web page containing an svg diagram..
func (ui *webUI) dot(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path != "/" {
		http.NotFound(w, req)
		return
	}

	// Generate dot graph.
	args := []string{"svg"}
	vars := pprofVariables.makeCopy()
	vars["focus"].value = req.URL.Query().Get("f")
	vars["show"].value = req.URL.Query().Get("s")
	vars["ignore"].value = req.URL.Query().Get("i")
	vars["hide"].value = req.URL.Query().Get("h")
	_, rpt, err := generateRawReport(ui.prof, args, vars, ui.options)
	if err != nil {
		reportHandlerError(w, "could not generate report", err)
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
		reportHandlerError(w, "Failed to execute dot. Is Graphviz installed?", err)
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
		Svg    template.HTML
		Legend []string
		Nodes  []string
	}{
		Title:  file + " " + profile,
		Svg:    template.HTML(string(svg)),
		Legend: legend,
		Nodes:  nodes,
	}
	html := &bytes.Buffer{}
	if err := graphTemplate.Execute(html, data); err != nil {
		reportHandlerError(w, "internal template error", err)
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
func (ui *webUI) disasm(w http.ResponseWriter, req *http.Request) {
	ui.output(w, req, "disasm", "text/plain")
}

// weblist generates a web page containing disassembly.
func (ui *webUI) weblist(w http.ResponseWriter, req *http.Request) {
	ui.output(w, req, "weblist", "text/html")
}

// output generates a webpage that contains the output of the specified pprof cmd.
func (ui *webUI) output(w http.ResponseWriter, req *http.Request, cmd, ctype string) {
	focus := req.URL.Query().Get("f")
	if focus == "" {
		fmt.Fprintln(w, "no argument supplied for "+cmd)
		return
	}
	args := []string{cmd, focus}
	vars := pprofVariables.makeCopy()
	_, rpt, err := generateRawReport(ui.prof, args, vars, ui.options)
	if err != nil {
		reportHandlerError(w, "error generating report", err)
		return
	}

	out := &bytes.Buffer{}
	if err := report.Generate(out, rpt, ui.options.Obj); err != nil {
		reportHandlerError(w, "error generating report", err)
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

func reportHandlerError(w http.ResponseWriter, msg string, err error) {
	fmt.Fprintf(os.Stderr, "%s: %v\n", msg, err)
	http.Error(w, msg, http.StatusInternalServerError)
}
