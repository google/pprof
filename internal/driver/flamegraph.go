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
	"html/template"
	"net/http"

	"github.com/google/pprof/internal/report"
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
	rpt, errList := ui.makeReport(w, req, []string{"svg"})
	if rpt == nil {
		return // error already reported
	}

	// Obtaining prof from report
	prof := rpt.Prof()

	// Get sample_index from variables
	si := pprofVariables["sample_index"].value

	// Getting default index
	index, err := ui.prof.SampleIndexByName(si)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		ui.options.UI.PrintErr(err)
		return
	}

	// Creating empty flame graph structure
	rootNode := flameGraphNode{"root", 0, make(map[string]*flameGraphNode)}

	// Walking sample structure and creating flame graph
	for _, sa := range prof.Sample {
		stack := []string{}
		for _, lo := range sa.Location {
			for _, li := range lo.Line {
				stack = append(stack, li.Function.Name)
			}
		}
		value := sa.Value[index]
		rootNode.add(&stack, len(stack)-1, value)
	}

	// JSON marshalling flame graph
	b, err := rootNode.MarshalJSON()
	if err != nil {
		http.Error(w, "error serializing flame graph", http.StatusInternalServerError)
		ui.options.UI.PrintErr(err)
		return
	}

	legend := report.ProfileLabels(rpt)
	file := getFromLegend(legend, "File: ", "unknown")

	ui.render(w, "/flamegraph", "flamegraph", rpt, errList, legend, webArgs{
		Title:          file,
		FlameGraph:     template.JS(b),
		FlameGraphUnit: prof.SampleType[index].Unit,
	})
}
