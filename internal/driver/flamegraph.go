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
	"encoding/json"
	"fmt"
	"html/template"
	"net/http"
	"path/filepath"
	"time"
)

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
		Name     string           `json:"n"`
		Value    int64            `json:"v"`
		Children []flameGraphNode `json:"c"`
	}{
		Name:     n.Name,
		Value:    n.Value,
		Children: v,
	})
}

// flamegraph generates a web page containing a flamegraph.
func (ui *webInterface) flamegraph(w http.ResponseWriter, req *http.Request) {
	// Capture any error messages generated while generating a report.
	catcher := &errorCatcher{UI: ui.options.UI}
	options := *ui.options
	options.UI = catcher

	// Get sample_index from variables
	si := pprofVariables["sample_index"].value

	// Get query parameters
	t := req.URL.Query().Get("t")

	// Defaulting to first SampleType in profile
	index := 0

	if t != "" {
		index, _ = ui.prof.SampleIndexByName(t)
	} else if si != "" {
		index, _ = ui.prof.SampleIndexByName(si)
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

	// Looking for profile metadata
	const layout = "Jan 2, 2006 at 3:04pm (MST)"
	file := "unknown"
	if ui.prof.Mapping[0].File != "" {
		file = filepath.Base(ui.prof.Mapping[0].File)
	}
	profileType := ui.prof.SampleType[index].Type
	profileUnit := ui.prof.SampleType[index].Unit

	profileTime := time.Unix(0, ui.prof.TimeNanos).Format(layout)
	profileDuration := fmt.Sprintf("%d ns", ui.prof.DurationNanos)
	if ui.prof.DurationNanos > 1000000000 {
		profileDuration = fmt.Sprintf("%f s", float64(ui.prof.DurationNanos)/1000000000)
	}

	// Creating list of profile types
	profileTypes := []string{}
	for _, s := range ui.prof.SampleType {
		profileTypes = append(profileTypes, s.Type)
	}

	legendUnit := profileUnit
	if legendUnit == "nanoseconds" {
		legendUnit = "seconds"
	}

	legend := []string{
		"File: " + file,
		"Type: " + profileType,
		"Unit: " + legendUnit,
		"Time: " + profileTime,
		"Duration: " + profileDuration,
	}

	rpt, errList := ui.makeReport(w, req, []string{"svg"})
	if rpt == nil {
		return // error already reported
	}

	ui.render(w, "/flamegraph", "flamegraph", rpt, errList, legend, webArgs{
		Title:          file,
		FlameGraph:     template.JS(b),
		FlameGraphUnit: profileUnit,
	})
}
