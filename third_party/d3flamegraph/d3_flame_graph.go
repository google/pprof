// D3.js is a JavaScript library for manipulating documents based on data.
// https://github.com/d3/d3
// See D3_LICENSE file for license details

// d3-flame-graph is a D3.js plugin that produces flame graphs from hierarchical data.
// https://github.com/spiermar/d3-flame-graph
// See D3_FLAME_GRAPH_LICENSE file for license details

package d3flamegraph

import _ "embed"

// JSSource returns the d3 and d3-flame-graph JavaScript bundle
//
//go:embed d3.js
var JSSource string

// CSSSource returns the d3-flamegraph.css file
//
//go:embed d3.css
var CSSSource string
