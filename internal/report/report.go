// Copyright 2014 Google Inc. All Rights Reserved.
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

// Package report summarizes a performance profile into a
// human-readable report.
package report

import (
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/google/pprof/internal/graph"
	"github.com/google/pprof/internal/measurement"
	"github.com/google/pprof/internal/plugin"
	"github.com/google/pprof/profile"
)

// Generate generates a report as directed by the Report.
func Generate(w io.Writer, rpt *Report, obj plugin.ObjTool) error {
	o := rpt.options

	switch o.OutputFormat {
	case Dot:
		return printDOT(w, rpt)
	case Tree:
		return printTree(w, rpt)
	case Text:
		return printText(w, rpt)
	case Traces:
		return printTraces(w, rpt)
	case Raw:
		fmt.Fprint(w, rpt.prof.String())
		return nil
	case Tags:
		return printTags(w, rpt)
	case Proto:
		return rpt.prof.Write(w)
	case TopProto:
		return printTopProto(w, rpt)
	case Dis:
		return printAssembly(w, rpt, obj)
	case List:
		return printSource(w, rpt)
	case WebList:
		return printWebSource(w, rpt, obj)
	case Callgrind:
		return printCallgrind(w, rpt)
	}
	return fmt.Errorf("unexpected output format")
}

// newTrimmedGraph creates a graph for this report, trimmed according
// to the report options.
func (rpt *Report) newTrimmedGraph() (g *graph.Graph, origCount, droppedNodes, droppedEdges int) {
	o := rpt.options

	// Build a graph and refine it. On each refinement step we must rebuild the graph from the samples,
	// as the graph itself doesn't contain enough information to preserve full precision.
	visualMode := o.OutputFormat == Dot
	cumSort := o.CumSort

	// First step: Build complete graph to identify low frequency nodes, based on their cum weight.
	g = rpt.newGraph(nil)
	totalValue, _ := g.Nodes.Sum()
	nodeCutoff := abs64(int64(float64(totalValue) * o.NodeFraction))
	edgeCutoff := abs64(int64(float64(totalValue) * o.EdgeFraction))

	// Do not apply edge cutoff to preserve tree structure.
	if o.CallTree {
		if o.OutputFormat == Dot {
			fmt.Println("WARNING: Trimming trees is unsupported.")
			fmt.Printf("Tree will contain at least %d nodes\n", o.NodeCount)
			cumSort = true
		}
		edgeCutoff = 0
	}

	// Filter out nodes with cum value below nodeCutoff.
	if nodeCutoff > 0 {
		if nodesKept := g.DiscardLowFrequencyNodes(nodeCutoff); len(g.Nodes) != len(nodesKept) {
			droppedNodes = len(g.Nodes) - len(nodesKept)
			g = rpt.newGraph(nodesKept)
		}
	}
	origCount = len(g.Nodes)

	// Second step: Limit the total number of nodes. Apply specialized heuristics to improve
	// visualization when generating dot output.
	g.SortNodes(cumSort, visualMode)
	if nodeCount := o.NodeCount; nodeCount > 0 {
		// Remove low frequency tags and edges as they affect selection.
		g.TrimLowFrequencyTags(nodeCutoff)
		g.TrimLowFrequencyEdges(edgeCutoff)
		if nodesKept := g.SelectTopNodes(nodeCount, visualMode); len(nodesKept) != len(g.Nodes) {
			g = rpt.newGraph(nodesKept)
			g.SortNodes(cumSort, visualMode)
		}
	}

	// Final step: Filter out low frequency tags and edges, and remove redundant edges that clutter
	// the graph.
	g.TrimLowFrequencyTags(nodeCutoff)
	droppedEdges = g.TrimLowFrequencyEdges(edgeCutoff)
	if visualMode {
		g.RemoveRedundantEdges()
	}
	return
}

func (rpt *Report) selectOutputUnit(g *graph.Graph) {
	o := rpt.options

	// Select best unit for profile output.
	// Find the appropriate units for the smallest non-zero sample
	if o.OutputUnit != "minimum" || len(g.Nodes) == 0 {
		return
	}
	var minValue int64

	for _, n := range g.Nodes {
		nodeMin := abs64(n.Flat)
		if nodeMin == 0 {
			nodeMin = abs64(n.Cum)
		}
		if nodeMin > 0 && (minValue == 0 || nodeMin < minValue) {
			minValue = nodeMin
		}
	}
	maxValue := rpt.total
	if minValue == 0 {
		minValue = maxValue
	}

	if r := o.Ratio; r > 0 && r != 1 {
		minValue = int64(float64(minValue) * r)
		maxValue = int64(float64(maxValue) * r)
	}

	_, minUnit := measurement.Scale(minValue, o.SampleUnit, "minimum")
	_, maxUnit := measurement.Scale(maxValue, o.SampleUnit, "minimum")

	unit := minUnit
	if minUnit != maxUnit && minValue*100 < maxValue && o.OutputFormat != Callgrind {
		// Minimum and maximum values have different units. Scale
		// minimum by 100 to use larger units, allowing minimum value to
		// be scaled down to 0.01, except for callgrind reports since
		// they can only represent integer values.
		_, unit = measurement.Scale(100*minValue, o.SampleUnit, "minimum")
	}

	if unit != "" {
		o.OutputUnit = unit
	} else {
		o.OutputUnit = o.SampleUnit
	}
}

// newGraph creates a new graph for this report. If nodes is non-nil,
// only nodes whose info matches are included. Otherwise, all nodes
// are included, without trimming.
func (rpt *Report) newGraph(nodes graph.NodeSet) *graph.Graph {
	o := rpt.options

	// Clean up file paths using heuristics.
	prof := rpt.prof
	for _, f := range prof.Function {
		f.Filename = trimPath(f.Filename)
	}
	// Remove numeric tags not recognized by pprof.
	for _, s := range prof.Sample {
		numLabels := make(map[string][]int64, len(s.NumLabel))
		for k, v := range s.NumLabel {
			if k == "bytes" {
				numLabels[k] = append(numLabels[k], v...)
			}
		}
		s.NumLabel = numLabels
	}

	gopt := &graph.Options{
		SampleValue:  o.SampleValue,
		FormatTag:    formatTag,
		CallTree:     o.CallTree && o.OutputFormat == Dot,
		DropNegative: o.DropNegative,
		KeptNodes:    nodes,
	}

	// Only keep binary names for disassembly-based reports, otherwise
	// remove it to allow merging of functions across binaries.
	switch o.OutputFormat {
	case Raw, List, WebList, Dis:
		gopt.ObjNames = true
	}

	return graph.New(rpt.prof, gopt)
}

func formatTag(v int64, key string) string {
	return measurement.Label(v, key)
}

func printTopProto(w io.Writer, rpt *Report) error {
	p := rpt.prof
	o := rpt.options
	g, _, _, _ := rpt.newTrimmedGraph()
	rpt.selectOutputUnit(g)

	out := profile.Profile{
		SampleType: []*profile.ValueType{
			{Type: "cum", Unit: o.OutputUnit},
			{Type: "flat", Unit: o.OutputUnit},
		},
		TimeNanos:     p.TimeNanos,
		DurationNanos: p.DurationNanos,
		PeriodType:    p.PeriodType,
		Period:        p.Period,
	}
	var flatSum int64
	for i, n := range g.Nodes {
		name, flat, cum := n.Info.PrintableName(), n.Flat, n.Cum

		flatSum += flat
		f := &profile.Function{
			ID:         uint64(i + 1),
			Name:       name,
			SystemName: name,
		}
		l := &profile.Location{
			ID: uint64(i + 1),
			Line: []profile.Line{
				{
					Function: f,
				},
			},
		}

		fv, _ := measurement.Scale(flat, o.SampleUnit, o.OutputUnit)
		cv, _ := measurement.Scale(cum, o.SampleUnit, o.OutputUnit)
		s := &profile.Sample{
			Location: []*profile.Location{l},
			Value:    []int64{int64(cv), int64(fv)},
		}
		out.Function = append(out.Function, f)
		out.Location = append(out.Location, l)
		out.Sample = append(out.Sample, s)
	}

	return out.Write(w)
}

// printAssembly prints an annotated assembly listing.
func printAssembly(w io.Writer, rpt *Report, obj plugin.ObjTool) error {
	o := rpt.options
	prof := rpt.prof

	g := rpt.newGraph(nil)

	// If the regexp source can be parsed as an address, also match
	// functions that land on that address.
	var address *uint64
	if hex, err := strconv.ParseUint(o.Symbol.String(), 0, 64); err == nil {
		address = &hex
	}

	fmt.Fprintln(w, "Total:", rpt.formatValue(rpt.total))
	symbols := symbolsFromBinaries(prof, g, o.Symbol, address, obj)
	symNodes := nodesPerSymbol(g.Nodes, symbols)
	// Sort function names for printing.
	var syms objSymbols
	for s := range symNodes {
		syms = append(syms, s)
	}
	sort.Sort(syms)

	// Correlate the symbols from the binary with the profile samples.
	for _, s := range syms {
		sns := symNodes[s]

		// Gather samples for this symbol.
		flatSum, cumSum := sns.Sum()

		// Get the function assembly.
		insns, err := obj.Disasm(s.sym.File, s.sym.Start, s.sym.End)
		if err != nil {
			return err
		}

		ns := annotateAssembly(insns, sns, s.base)

		fmt.Fprintf(w, "ROUTINE ======================== %s\n", s.sym.Name[0])
		for _, name := range s.sym.Name[1:] {
			fmt.Fprintf(w, "    AKA ======================== %s\n", name)
		}
		fmt.Fprintf(w, "%10s %10s (flat, cum) %s of Total\n",
			rpt.formatValue(flatSum), rpt.formatValue(cumSum),
			percentage(cumSum, rpt.total))

		for _, n := range ns {
			fmt.Fprintf(w, "%10s %10s %10x: %s\n", valueOrDot(n.Flat, rpt), valueOrDot(n.Cum, rpt), n.Info.Address, n.Info.Name)
		}
	}
	return nil
}

// symbolsFromBinaries examines the binaries listed on the profile
// that have associated samples, and identifies symbols matching rx.
func symbolsFromBinaries(prof *profile.Profile, g *graph.Graph, rx *regexp.Regexp, address *uint64, obj plugin.ObjTool) []*objSymbol {
	hasSamples := make(map[string]bool)
	// Only examine mappings that have samples that match the
	// regexp. This is an optimization to speed up pprof.
	for _, n := range g.Nodes {
		if name := n.Info.PrintableName(); rx.MatchString(name) && n.Info.Objfile != "" {
			hasSamples[n.Info.Objfile] = true
		}
	}

	// Walk all mappings looking for matching functions with samples.
	var objSyms []*objSymbol
	for _, m := range prof.Mapping {
		if !hasSamples[filepath.Base(m.File)] {
			if address == nil || !(m.Start <= *address && *address <= m.Limit) {
				continue
			}
		}

		f, err := obj.Open(m.File, m.Start, m.Limit, m.Offset)
		if err != nil {
			fmt.Printf("%v\n", err)
			continue
		}

		// Find symbols in this binary matching the user regexp.
		var addr uint64
		if address != nil {
			addr = *address
		}
		msyms, err := f.Symbols(rx, addr)
		base := f.Base()
		f.Close()
		if err != nil {
			continue
		}
		for _, ms := range msyms {
			objSyms = append(objSyms,
				&objSymbol{
					sym:  ms,
					base: base,
				},
			)
		}
	}

	return objSyms
}

// objSym represents a symbol identified from a binary. It includes
// the SymbolInfo from the disasm package and the base that must be
// added to correspond to sample addresses
type objSymbol struct {
	sym  *plugin.Sym
	base uint64
}

// objSymbols is a wrapper type to enable sorting of []*objSymbol.
type objSymbols []*objSymbol

func (o objSymbols) Len() int {
	return len(o)
}

func (o objSymbols) Less(i, j int) bool {
	if namei, namej := o[i].sym.Name[0], o[j].sym.Name[0]; namei != namej {
		return namei < namej
	}
	return o[i].sym.Start < o[j].sym.Start
}

func (o objSymbols) Swap(i, j int) {
	o[i], o[j] = o[j], o[i]
}

// nodesPerSymbol classifies nodes into a group of symbols.
func nodesPerSymbol(ns graph.Nodes, symbols []*objSymbol) map[*objSymbol]graph.Nodes {
	symNodes := make(map[*objSymbol]graph.Nodes)
	for _, s := range symbols {
		// Gather samples for this symbol.
		for _, n := range ns {
			address := n.Info.Address - s.base
			if address >= s.sym.Start && address < s.sym.End {
				symNodes[s] = append(symNodes[s], n)
			}
		}
	}
	return symNodes
}

// annotateAssembly annotates a set of assembly instructions with a
// set of samples. It returns a set of nodes to display.  base is an
// offset to adjust the sample addresses.
func annotateAssembly(insns []plugin.Inst, samples graph.Nodes, base uint64) graph.Nodes {
	// Add end marker to simplify printing loop.
	insns = append(insns, plugin.Inst{^uint64(0), "", "", 0})

	// Ensure samples are sorted by address.
	samples.Sort(graph.AddressOrder)

	var s int
	var asm graph.Nodes
	for ix, in := range insns[:len(insns)-1] {
		n := graph.Node{
			Info: graph.NodeInfo{
				Address: in.Addr,
				Name:    in.Text,
				File:    trimPath(in.File),
				Lineno:  in.Line,
			},
		}

		// Sum all the samples until the next instruction (to account
		// for samples attributed to the middle of an instruction).
		for next := insns[ix+1].Addr; s < len(samples) && samples[s].Info.Address-base < next; s++ {
			n.Flat += samples[s].Flat
			n.Cum += samples[s].Cum
			if samples[s].Info.File != "" {
				n.Info.File = trimPath(samples[s].Info.File)
				n.Info.Lineno = samples[s].Info.Lineno
			}
		}
		asm = append(asm, &n)
	}

	return asm
}

// valueOrDot formats a value according to a report, intercepting zero
// values.
func valueOrDot(value int64, rpt *Report) string {
	if value == 0 {
		return "."
	}
	return rpt.formatValue(value)
}

// canAccessFile determines if the filename can be opened for reading.
func canAccessFile(path string) bool {
	if fi, err := os.Stat(path); err == nil {
		return fi.Mode().Perm()&0400 != 0
	}
	return false
}

// printTags collects all tags referenced in the profile and prints
// them in a sorted table.
func printTags(w io.Writer, rpt *Report) error {
	p := rpt.prof

	// Hashtable to keep accumulate tags as key,value,count.
	tagMap := make(map[string]map[string]int64)
	for _, s := range p.Sample {
		for key, vals := range s.Label {
			for _, val := range vals {
				if valueMap, ok := tagMap[key]; ok {
					valueMap[val] = valueMap[val] + s.Value[0]
					continue
				}
				valueMap := make(map[string]int64)
				valueMap[val] = s.Value[0]
				tagMap[key] = valueMap
			}
		}
		for key, vals := range s.NumLabel {
			for _, nval := range vals {
				val := measurement.Label(nval, key)
				if valueMap, ok := tagMap[key]; ok {
					valueMap[val] = valueMap[val] + s.Value[0]
					continue
				}
				valueMap := make(map[string]int64)
				valueMap[val] = s.Value[0]
				tagMap[key] = valueMap
			}
		}
	}

	tagKeys := make([]*graph.Tag, 0, len(tagMap))
	for key := range tagMap {
		tagKeys = append(tagKeys, &graph.Tag{Name: key})
	}
	for _, tagKey := range graph.SortTags(tagKeys, true) {
		var total int64
		key := tagKey.Name
		tags := make([]*graph.Tag, 0, len(tagMap[key]))
		for t, c := range tagMap[key] {
			total += c
			tags = append(tags, &graph.Tag{Name: t, Flat: c})
		}

		fmt.Fprintf(w, "%s: Total %d\n", key, total)
		for _, t := range graph.SortTags(tags, true) {
			if total > 0 {
				fmt.Fprintf(w, "  %8d (%s): %s\n", t.Flat,
					percentage(t.Flat, total), t.Name)
			} else {
				fmt.Fprintf(w, "  %8d: %s\n", t.Flat, t.Name)
			}
		}
		fmt.Fprintln(w)
	}
	return nil
}

// printText prints a flat text report for a profile.
func printText(w io.Writer, rpt *Report) error {
	g, origCount, droppedNodes, _ := rpt.newTrimmedGraph()
	rpt.selectOutputUnit(g)

	fmt.Fprintln(w, strings.Join(reportLabels(rpt, g, origCount, droppedNodes, 0, false), "\n"))

	fmt.Fprintf(w, "%10s %5s%% %5s%% %10s %5s%%\n",
		"flat", "flat", "sum", "cum", "cum")

	var flatSum int64
	for _, n := range g.Nodes {
		name, flat, cum := n.Info.PrintableName(), n.Flat, n.Cum

		var inline, noinline bool
		for _, e := range n.In {
			if e.Inline {
				inline = true
			} else {
				noinline = true
			}
		}

		if inline {
			if noinline {
				name = name + " (partial-inline)"
			} else {
				name = name + " (inline)"
			}
		}

		flatSum += flat
		fmt.Fprintf(w, "%10s %s %s %10s %s  %s\n",
			rpt.formatValue(flat),
			percentage(flat, rpt.total),
			percentage(flatSum, rpt.total),
			rpt.formatValue(cum),
			percentage(cum, rpt.total),
			name)
	}
	return nil
}

// printTraces prints all traces from a profile.
func printTraces(w io.Writer, rpt *Report) error {
	fmt.Fprintln(w, strings.Join(ProfileLabels(rpt), "\n"))

	prof := rpt.prof
	o := rpt.options

	const separator = "-----------+-------------------------------------------------------"

	_, locations := graph.CreateNodes(prof, false, nil)
	for _, sample := range prof.Sample {
		var stack graph.Nodes
		for _, loc := range sample.Location {
			id := loc.ID
			stack = append(stack, locations[id]...)
		}

		if len(stack) == 0 {
			continue
		}

		fmt.Fprintln(w, separator)
		// Print any text labels for the sample.
		var labels []string
		for s, vs := range sample.Label {
			labels = append(labels, fmt.Sprintf("%10s:  %s\n", s, strings.Join(vs, " ")))
		}
		sort.Strings(labels)
		fmt.Fprint(w, strings.Join(labels, ""))
		// Print call stack.
		fmt.Fprintf(w, "%10s   %s\n",
			rpt.formatValue(o.SampleValue(sample.Value)),
			stack[0].Info.PrintableName())

		for _, s := range stack[1:] {
			fmt.Fprintf(w, "%10s   %s\n", "", s.Info.PrintableName())
		}
	}
	fmt.Fprintln(w, separator)
	return nil
}

// printCallgrind prints a graph for a profile on callgrind format.
func printCallgrind(w io.Writer, rpt *Report) error {
	o := rpt.options
	rpt.options.NodeFraction = 0
	rpt.options.EdgeFraction = 0
	rpt.options.NodeCount = 0

	g, _, _, _ := rpt.newTrimmedGraph()
	rpt.selectOutputUnit(g)

	fmt.Fprintln(w, "events:", o.SampleType+"("+o.OutputUnit+")")

	files := make(map[string]int)
	names := make(map[string]int)
	for _, n := range g.Nodes {
		fmt.Fprintln(w, "fl="+callgrindName(files, n.Info.File))
		fmt.Fprintln(w, "fn="+callgrindName(names, n.Info.Name))
		sv, _ := measurement.Scale(n.Flat, o.SampleUnit, o.OutputUnit)
		fmt.Fprintf(w, "%d %d\n", n.Info.Lineno, int64(sv))

		// Print outgoing edges.
		for _, out := range n.Out.Sort() {
			c, _ := measurement.Scale(out.Weight, o.SampleUnit, o.OutputUnit)
			callee := out.Dest
			fmt.Fprintln(w, "cfl="+callgrindName(files, callee.Info.File))
			fmt.Fprintln(w, "cfn="+callgrindName(names, callee.Info.Name))
			// pprof doesn't have a flat weight for a call, leave as 0.
			fmt.Fprintln(w, "calls=0", callee.Info.Lineno)
			fmt.Fprintln(w, n.Info.Lineno, int64(c))
		}
		fmt.Fprintln(w)
	}

	return nil
}

// callgrindName implements the callgrind naming compression scheme.
// For names not previously seen returns "(N) name", where N is a
// unique index.  For names previously seen returns "(N)" where N is
// the index returned the first time.
func callgrindName(names map[string]int, name string) string {
	if name == "" {
		return ""
	}
	if id, ok := names[name]; ok {
		return fmt.Sprintf("(%d)", id)
	}
	id := len(names) + 1
	names[name] = id
	return fmt.Sprintf("(%d) %s", id, name)
}

// printTree prints a tree-based report in text form.
func printTree(w io.Writer, rpt *Report) error {
	const separator = "----------------------------------------------------------+-------------"
	const legend = "      flat  flat%   sum%        cum   cum%   calls calls% + context 	 	 "

	g, origCount, droppedNodes, _ := rpt.newTrimmedGraph()
	rpt.selectOutputUnit(g)

	fmt.Fprintln(w, strings.Join(reportLabels(rpt, g, origCount, droppedNodes, 0, false), "\n"))

	fmt.Fprintln(w, separator)
	fmt.Fprintln(w, legend)
	var flatSum int64

	rx := rpt.options.Symbol
	for _, n := range g.Nodes {
		name, flat, cum := n.Info.PrintableName(), n.Flat, n.Cum

		// Skip any entries that do not match the regexp (for the "peek" command).
		if rx != nil && !rx.MatchString(name) {
			continue
		}

		fmt.Fprintln(w, separator)
		// Print incoming edges.
		inEdges := n.In.Sort()
		for _, in := range inEdges {
			var inline string
			if in.Inline {
				inline = " (inline)"
			}
			fmt.Fprintf(w, "%50s %s |   %s%s\n", rpt.formatValue(in.Weight),
				percentage(in.Weight, cum), in.Src.Info.PrintableName(), inline)
		}

		// Print current node.
		flatSum += flat
		fmt.Fprintf(w, "%10s %s %s %10s %s                | %s\n",
			rpt.formatValue(flat),
			percentage(flat, rpt.total),
			percentage(flatSum, rpt.total),
			rpt.formatValue(cum),
			percentage(cum, rpt.total),
			name)

		// Print outgoing edges.
		outEdges := n.Out.Sort()
		for _, out := range outEdges {
			var inline string
			if out.Inline {
				inline = " (inline)"
			}
			fmt.Fprintf(w, "%50s %s |   %s%s\n", rpt.formatValue(out.Weight),
				percentage(out.Weight, cum), out.Dest.Info.PrintableName(), inline)
		}
	}
	if len(g.Nodes) > 0 {
		fmt.Fprintln(w, separator)
	}
	return nil
}

// printDOT prints an annotated callgraph in DOT format.
func printDOT(w io.Writer, rpt *Report) error {
	g, origCount, droppedNodes, droppedEdges := rpt.newTrimmedGraph()
	rpt.selectOutputUnit(g)
	labels := reportLabels(rpt, g, origCount, droppedNodes, droppedEdges, true)

	c := &graph.DotConfig{
		Title:       rpt.options.Title,
		Labels:      labels,
		FormatValue: rpt.formatValue,
		Total:       rpt.total,
	}
	graph.ComposeDot(w, g, &graph.DotAttributes{}, c)
	return nil
}

// percentage computes the percentage of total of a value, and encodes
// it as a string. At least two digits of precision are printed.
func percentage(value, total int64) string {
	var ratio float64
	if total != 0 {
		ratio = math.Abs(float64(value)/float64(total)) * 100
	}
	switch {
	case math.Abs(ratio) >= 99.95 && math.Abs(ratio) <= 100.05:
		return "  100%"
	case math.Abs(ratio) >= 1.0:
		return fmt.Sprintf("%5.2f%%", ratio)
	default:
		return fmt.Sprintf("%5.2g%%", ratio)
	}
}

// ProfileLabels returns printable labels for a profile.
func ProfileLabels(rpt *Report) []string {
	label := []string{}
	prof := rpt.prof
	o := rpt.options
	if len(prof.Mapping) > 0 {
		if prof.Mapping[0].File != "" {
			label = append(label, "File: "+filepath.Base(prof.Mapping[0].File))
		}
		if prof.Mapping[0].BuildID != "" {
			label = append(label, "Build ID: "+prof.Mapping[0].BuildID)
		}
	}
	label = append(label, prof.Comments...)
	if o.SampleType != "" {
		label = append(label, "Type: "+o.SampleType)
	}
	if prof.TimeNanos != 0 {
		const layout = "Jan 2, 2006 at 3:04pm (MST)"
		label = append(label, "Time: "+time.Unix(0, prof.TimeNanos).Format(layout))
	}
	if prof.DurationNanos != 0 {
		duration := measurement.Label(prof.DurationNanos, "nanoseconds")
		totalNanos, totalUnit := measurement.Scale(rpt.total, o.SampleUnit, "nanoseconds")
		var ratio string
		if totalUnit == "ns" && totalNanos != 0 {
			ratio = "(" + percentage(int64(totalNanos), prof.DurationNanos) + ")"
		}
		label = append(label, fmt.Sprintf("Duration: %s, Total samples = %s %s", duration, rpt.formatValue(rpt.total), ratio))
	}
	return label
}

// reportLabels returns printable labels for a report. Includes
// profileLabels.
func reportLabels(rpt *Report, g *graph.Graph, origCount, droppedNodes, droppedEdges int, fullHeaders bool) []string {
	nodeFraction := rpt.options.NodeFraction
	edgeFraction := rpt.options.EdgeFraction
	nodeCount := len(g.Nodes)

	var label []string
	if len(rpt.options.ProfileLabels) > 0 {
		for _, l := range rpt.options.ProfileLabels {
			label = append(label, l)
		}
	} else if fullHeaders || !rpt.options.CompactLabels {
		label = ProfileLabels(rpt)
	}

	var flatSum int64
	for _, n := range g.Nodes {
		flatSum = flatSum + n.Flat
	}

	label = append(label, fmt.Sprintf("Showing nodes accounting for %s, %s of %s total", rpt.formatValue(flatSum), strings.TrimSpace(percentage(flatSum, rpt.total)), rpt.formatValue(rpt.total)))

	if rpt.total != 0 {
		if droppedNodes > 0 {
			label = append(label, genLabel(droppedNodes, "node", "cum",
				rpt.formatValue(abs64(int64(float64(rpt.total)*nodeFraction)))))
		}
		if droppedEdges > 0 {
			label = append(label, genLabel(droppedEdges, "edge", "freq",
				rpt.formatValue(abs64(int64(float64(rpt.total)*edgeFraction)))))
		}
		if nodeCount > 0 && nodeCount < origCount {
			label = append(label, fmt.Sprintf("Showing top %d nodes out of %d",
				nodeCount, origCount))
		}
	}
	return label
}

func genLabel(d int, n, l, f string) string {
	if d > 1 {
		n = n + "s"
	}
	return fmt.Sprintf("Dropped %d %s (%s <= %s)", d, n, l, f)
}

// Output formats.
const (
	Proto = iota
	Dot
	Tags
	Tree
	Text
	Traces
	Raw
	Dis
	List
	WebList
	Callgrind
	TopProto
)

// Options are the formatting and filtering options used to generate a
// profile.
type Options struct {
	OutputFormat int

	CumSort             bool
	CallTree            bool
	DropNegative        bool
	PositivePercentages bool
	CompactLabels       bool
	Ratio               float64
	Title               string
	ProfileLabels       []string

	NodeCount    int
	NodeFraction float64
	EdgeFraction float64

	SampleValue func(s []int64) int64
	SampleType  string
	SampleUnit  string // Unit for the sample data from the profile.

	OutputUnit string // Units for data formatting in report.

	Symbol     *regexp.Regexp // Symbols to include on disassembly report.
	SourcePath string         // Search path for source files.
}

// New builds a new report indexing the sample values interpreting the
// samples with the provided function.
func New(prof *profile.Profile, o *Options) *Report {
	format := func(v int64) string {
		if r := o.Ratio; r > 0 && r != 1 {
			fv := float64(v) * r
			v = int64(fv)
		}
		return measurement.ScaledLabel(v, o.SampleUnit, o.OutputUnit)
	}
	return &Report{prof, computeTotal(prof, o.SampleValue, !o.PositivePercentages),
		o, format}
}

// NewDefault builds a new report indexing the last sample value
// available.
func NewDefault(prof *profile.Profile, options Options) *Report {
	index := len(prof.SampleType) - 1
	o := &options
	if o.Title == "" && len(prof.Mapping) > 0 && prof.Mapping[0].File != "" {
		o.Title = filepath.Base(prof.Mapping[0].File)
	}
	o.SampleType = prof.SampleType[index].Type
	o.SampleUnit = strings.ToLower(prof.SampleType[index].Unit)
	o.SampleValue = func(v []int64) int64 {
		return v[index]
	}
	return New(prof, o)
}

// computeTotal computes the sum of all sample values. This will be
// used to compute percentages. If includeNegative is set, use use
// absolute values to provide a meaningful percentage for both
// negative and positive values. Otherwise only use positive values,
// which is useful when comparing profiles from different jobs.
func computeTotal(prof *profile.Profile, value func(v []int64) int64, includeNegative bool) int64 {
	var ret int64
	for _, sample := range prof.Sample {
		if v := value(sample.Value); v > 0 {
			ret += v
		} else if includeNegative {
			ret -= v
		}
	}
	return ret
}

// Report contains the data and associated routines to extract a
// report from a profile.
type Report struct {
	prof        *profile.Profile
	total       int64
	options     *Options
	formatValue func(int64) string
}

func (rpt *Report) formatTags(s *profile.Sample) (string, bool) {
	var labels []string
	for key, vals := range s.Label {
		for _, v := range vals {
			labels = append(labels, key+":"+v)
		}
	}
	for key, nvals := range s.NumLabel {
		for _, v := range nvals {
			labels = append(labels, measurement.Label(v, key))
		}
	}
	if len(labels) == 0 {
		return "", false
	}
	sort.Strings(labels)
	return strings.Join(labels, `\n`), true
}

func abs64(i int64) int64 {
	if i < 0 {
		return -i
	}
	return i
}
