// Copyright © 2017 Martin Spier <spiermar@gmail.com>
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
	"encoding/json"
	"fmt"
	"html/template"
	"net/http"
	"path/filepath"
	"time"
)

var flameGraphTemplate = template.Must(template.New("graph").Parse(`<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1">

    <link rel="stylesheet" type="text/css" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css">
		<link rel="stylesheet" type="text/css" href="https://cdn.jsdelivr.net/gh/spiermar/d3-flame-graph@1.0.1/dist/d3.flameGraph.min.css">
		<style>
			/* Space out content a bit */
			body {
				padding-top: 20px;
				padding-bottom: 20px;
			}

			/* Custom page header */
			.header {
				padding-bottom: 15px;
				padding-right: 15px;
				padding-left: 15px;
				border-bottom: 1px solid #e5e5e5;
			}

			/* Make the masthead heading the same height as the navigation */
			.header h3 {
				margin-top: 0;
				margin-bottom: 0;
			}

			/* Customize container */
			.container {
				max-width: 990px;
			}

			/* Sample details */
			.details {
				height: 1.428em;
			}
    </style>

    <title>{{.Title}} {{.Type}}</title>

    <!-- HTML5 shim and Respond.js for IE8 support of HTML5 elements and media queries -->
    <!--[if lt IE 9]>
      <script src="https://oss.maxcdn.com/html5shiv/3.7.2/html5shiv.min.js"></script>
      <script src="https://oss.maxcdn.com/respond/1.4.2/respond.min.js"></script>
    <![endif]-->
  </head>
  <body>
    <div class="container">
      <div class="header clearfix">
        <nav>
          <div class="pull-right">  
            <form class="form-inline" id="form">
              <div class="form-group">
                <select class="form-control" id="type" onchange="location = this.value;">
                {{range .Types}}
                  <option value="?t={{.}}"{{if eq . $.Type}} selected{{end}}>{{.}}</option>
                {{end}}
                </select>
              </div>
              <a class="btn" href="javascript: resetZoom();">Reset zoom</a>
              <a class="btn" href="javascript: clear();">Clear</a>
              <div class="form-group">
                <input type="text" class="form-control" id="term">
              </div>
              <a class="btn btn-primary" href="javascript: search();">Search</a>
            </form>
          </div>
				</nav>
				<h3 class="text-muted">{{.Title}}</h3>
      </div>
      <div id="chart">
      </div>
      <hr>
      <div id="details" class="details">
			</div>
			<hr>
			<div id="profile" class="profile">
				<h4 class="text-muted">profile details:</h4>
				<dl class="row">
					<dt class="col-md-2">File</dt>
					<dd class="col-md-10">{{.Title}}</dd>
					<dt class="col-md-2">Type</dt>
					<dd class="col-md-10">{{.Type}}</dd>
					<dt class="col-md-2">Unit</dt>
					<dd class="col-md-10">{{.Unit}}</dd>
					<dt class="col-md-2">Time</dt>
					<dd class="col-md-10">{{.Time}}</dd>
					<dt class="col-md-2">Duration</dt>
					<dd class="col-md-10">{{.Duration}}</dd>
				</dl>
			</div>
    </div>

    <script type="text/javascript" src="https://cdnjs.cloudflare.com/ajax/libs/d3/4.10.0/d3.min.js"></script>
    <script type="text/javascript" src="https://cdnjs.cloudflare.com/ajax/libs/d3-tip/0.7.1/d3-tip.min.js"></script>
		<script type="text/javascript" src="https://cdnjs.cloudflare.com/ajax/libs/lodash.js/4.17.4/lodash.min.js"></script>
	  <script type="text/javascript" src="https://cdn.jsdelivr.net/gh/spiermar/d3-flame-graph@1.0.1/dist/d3.flameGraph.min.js"></script>
	  <script type="text/javascript">
		  var data = {{.Data}};
	  </script>
		<script type="text/javascript">
			var label = function(d) {
				return d.data.name + " (" + d3.format(".3f")(100 * (d.x1 - d.x0), 3) + "%, " + d.data.value + " {{.Unit}})";
			};

      var flameGraph = d3.flameGraph()
        .height(540)
        .width(960)
        .cellHeight(18)
        .transitionDuration(750)
        .transitionEase(d3.easeCubic)
        .sort(true)
				.title("")
				.label(label)
        .onClick(onClick);

      // Example on how to use custom tooltips using d3-tip.
      var tip = d3.tip()
        .direction("s")
        .offset([8, 0])
        .attr('class', 'd3-flame-graph-tip')
        .html(function(d) { return "name: " + d.data.name + ", value: " + d.data.value; });

      flameGraph.tooltip(tip);

      d3.select("#chart")
        .datum(data)
        .call(flameGraph);

      document.getElementById("form").addEventListener("submit", function(event){
        event.preventDefault();
        search();
      });

      function search() {
        var term = document.getElementById("term").value;
        flameGraph.search(term);
      }

      function clear() {
        document.getElementById('term').value = '';
        flameGraph.clear();
      }

      function resetZoom() {
        flameGraph.resetZoom();
      }

      function onClick(d) {
        console.info("Clicked on " + d.data.name);
      }
		</script>
  </body>
</html>`))

type flameGraphNode struct {
	Name     string
	Value    int64
	Children map[string]*flameGraphNode
}

func (n *flameGraphNode) add(stackPtr *[]string, index int, value int64) {
	n.Value += value
	if index >= 0 {
		head := (*stackPtr)[index]
		childPtr, ok := n.Children[head]
		if !ok {
			childPtr = &(flameGraphNode{head, 0, make(map[string]*flameGraphNode)})
			n.Children[head] = childPtr
		}
		childPtr.add(stackPtr, index-1, value)
	}
}

func (n *flameGraphNode) MarshalJSON() ([]byte, error) {
	v := make([]flameGraphNode, 0, len(n.Children))
	for _, value := range n.Children {
		v = append(v, *value)
	}

	return json.Marshal(&struct {
		Name     string           `json:"name"`
		Value    int64            `json:"value"`
		Children []flameGraphNode `json:"children"`
	}{
		Name:     n.Name,
		Value:    n.Value,
		Children: v,
	})
}

// flamegraph generates a web page containing a flamegraph.
func (ui *webInterface) flamegraph(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path != "/flamegraph" {
		http.NotFound(w, req)
		return
	}

	// Capture any error messages generated while generating a report.
	catcher := &errorCatcher{UI: ui.options.UI}
	options := *ui.options
	options.UI = catcher

	// Get query parameters
	t := req.URL.Query().Get("t")

	// Defaulting to first SampleType in profile
	index := 0

	if t != "" {
		for i, st := range ui.prof.SampleType {
			if st.Type == t {
				index = i
			}
		}
	}

	rootNode := flameGraphNode{"root", 0, make(map[string]*flameGraphNode)}

	for _, sa := range ui.prof.Sample {
		stack := []string{}
		for _, lo := range sa.Location {
			for _, li := range lo.Line {
				stack = append(stack, li.Function.Name)
			}
		}
		value := sa.Value[index]
		rootNode.add(&stack, len(stack)-1, value)
	}

	b, err := rootNode.MarshalJSON()
	if err != nil {
		http.Error(w, "error serializing flame graph", http.StatusInternalServerError)
		ui.options.UI.PrintErr(err)
		return
	}

	// Looking for file, profile type and unit
	const layout = "Jan 2, 2006 at 3:04pm (MST)"
	file := "unknown"
	if ui.prof.Mapping[0].File != "" {
		file = filepath.Base(ui.prof.Mapping[0].File)
	}
	profileType := ui.prof.SampleType[index].Type
	profileUnit := ui.prof.SampleType[index].Unit

	profileTime := time.Unix(0, ui.prof.TimeNanos).Format(layout)
	profileDuration := fmt.Sprintf("%d ns", ui.prof.DurationNanos)

	// Creating list of profile types
	profileTypes := []string{}
	for _, s := range ui.prof.SampleType {
		profileTypes = append(profileTypes, s.Type)
	}

	// Embed in html.
	data := struct {
		Title    string
		Type     string
		Unit     string
		Time     string
		Duration string
		Types    []string
		Errors   []string
		Data     template.JS
		Help     map[string]string
	}{
		Title:    file,
		Type:     profileType,
		Unit:     profileUnit,
		Time:     profileTime,
		Duration: profileDuration,
		Types:    profileTypes,
		Errors:   catcher.errors,
		Data:     template.JS(b),
		Help:     ui.help,
	}
	html := &bytes.Buffer{}
	if err := flameGraphTemplate.Execute(html, data); err != nil {
		http.Error(w, "internal template error", http.StatusInternalServerError)
		ui.options.UI.PrintErr(err)
		return
	}
	w.Header().Set("Content-Type", "text/html")
	w.Write(html.Bytes())
}
