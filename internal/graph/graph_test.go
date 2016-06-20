package graph

import (
	"fmt"
	"testing"
)

func edgeDebugString(edge *Edge) string {
	debug := ""
	debug += fmt.Sprintf("\t\tSrc: %p\n", edge.Src)
	debug += fmt.Sprintf("\t\tDest: %p\n", edge.Dest)
	debug += fmt.Sprintf("\t\tResidual: %t\n", edge.Residual)
	debug += fmt.Sprintf("\t\tInline: %t\n", edge.Inline)
	return debug
}

func edgeMapsDebugString(in, out EdgeMap) string {
	debug := ""
	debug += "In Edges:\n"
	for parent, edge := range in {
		debug += fmt.Sprintf("\tParent: %p\n", parent)
		debug += edgeDebugString(edge)
	}
	debug += "Out Edges:\n"
	for child, edge := range out {
		debug += fmt.Sprintf("\tChild: %p\n", child)
		debug += edgeDebugString(edge)
	}
	return debug
}

func graphDebugString(graph *Graph) string {
	debug := ""
	for i, node := range graph.Nodes {
		debug += fmt.Sprintf("Node %d: %p\n", i, node)
	}

	for i, node := range graph.Nodes {
		debug += "\n"
		debug += fmt.Sprintf("===  Node %d: %p  ===\n", i, node)
		debug += edgeMapsDebugString(node.In, node.Out)
	}
	return debug
}

func expectedNodesDebugString(Expected []ExpectedNode) string {
	debug := ""
	for i, node := range Expected {
		debug += fmt.Sprintf("Node %d: %p\n", i, node.Node)
	}

	for i, node := range Expected {
		debug += "\n"
		debug += fmt.Sprintf("===  Node %d: %p  ===\n", i, node.Node)
		debug += edgeMapsDebugString(node.In, node.Out)
	}
	return debug
}

// Checks if two edges are equal
func edgesEqual(this, that *Edge) bool {
	return this.Src == that.Src && this.Dest == that.Dest &&
		this.Residual == that.Residual && this.Inline == that.Inline
}

// Checks if all the edges in this equal all the edges in that.
func edgeMapsEqual(this, that EdgeMap) bool {
	if len(this) != len(that) {
		return false
	}
	for node, thisEdge := range this {
		if !edgesEqual(thisEdge, that[node]) {
			return false
		}
	}
	return true
}

// Check if node is equal to Expected
func nodesEqual(node *Node, Expected ExpectedNode) bool {
	return node == Expected.Node && edgeMapsEqual(node.In, Expected.In) &&
		edgeMapsEqual(node.Out, Expected.Out)
}

// Check if the graph equals the one templated by Expected.
func graphsEqual(graph *Graph, Expected []ExpectedNode) bool {
	if len(graph.Nodes) != len(Expected) {
		return false
	}
	ExpectedSet := make(map[*Node]ExpectedNode)
	for i := range Expected {
		ExpectedSet[Expected[i].Node] = Expected[i]
	}

	for _, node := range graph.Nodes {
		ExpectedNode, found := ExpectedSet[node]
		if !found || !nodesEqual(node, ExpectedNode) {
			return false
		}
	}
	return true
}

type ExpectedNode struct {
	Node    *Node
	In, Out EdgeMap
}

type TrimTreeTestCase struct {
	Initial  *Graph
	Expected []ExpectedNode
	Keep     NodeSet
}

// Makes the edge from parent to child residual
func makeExpectedEdgeResidual(parent, child ExpectedNode) {
	parent.Out[child.Node].Residual = true
	child.In[parent.Node].Residual = true
}

// Creates a directed edges from the parent to each of the children
func createEdges(parent *Node, children ...*Node) {
	for _, child := range children {
		edge := &Edge{
			Src:  parent,
			Dest: child,
		}
		parent.Out[child] = edge
		child.In[parent] = edge
	}
}

// Creates a node without any edges
func createEmptyNode() *Node {
	return &Node{
		In:  make(EdgeMap),
		Out: make(EdgeMap),
	}
}

// Creates an array of ExpectedNodes from nodes.
func createExpectedNodes(nodes ...*Node) ([]ExpectedNode, NodeSet) {
	Expected := make([]ExpectedNode, len(nodes))
	Keep := NodeSet{
		Ptr: make(map[*Node]bool, len(nodes)),
	}

	for i, node := range nodes {
		Expected[i] = ExpectedNode{
			Node: node,
			In:   make(EdgeMap),
			Out:  make(EdgeMap),
		}
		Keep.Ptr[node] = true
	}

	return Expected, Keep
}

// Creates a directed edges from the parent to each of the children
func createExpectedEdges(parent ExpectedNode, children ...ExpectedNode) {
	for _, child := range children {
		edge := &Edge{
			Src:  parent.Node,
			Dest: child.Node,
		}
		parent.Out[child.Node] = edge
		child.In[parent.Node] = edge
	}
}

// The first test case looks like:
//     0
//     |
//     1
//   /   \
//  2     3
//
// After Keeping 0, 2, 3. We should see:
//     0
//   /   \
//  2     3
func createTestCase1() TrimTreeTestCase {
	// Create Initial graph
	graph := &Graph{make(Nodes, 4)}
	nodes := graph.Nodes
	for i := range nodes {
		nodes[i] = createEmptyNode()
	}
	createEdges(nodes[0], nodes[1])
	createEdges(nodes[1], nodes[2], nodes[3])

	// Create Expected graph
	Expected, Keep := createExpectedNodes(nodes[0], nodes[2], nodes[3])
	createExpectedEdges(Expected[0], Expected[1], Expected[2])
	makeExpectedEdgeResidual(Expected[0], Expected[1])
	makeExpectedEdgeResidual(Expected[0], Expected[2])
	return TrimTreeTestCase{
		Initial:  graph,
		Expected: Expected,
		Keep:     Keep,
	}
}

// This test case looks like:
//   3
//   |
//   1
//   |
//   2
//   |
//   0
//   |
//   4
//
// After Keeping 3 and 4. We should see:
//   3
//   |
//   4
func createTestCase2() TrimTreeTestCase {
	// Create Initial graph
	graph := &Graph{make(Nodes, 5)}
	nodes := graph.Nodes
	for i := range nodes {
		nodes[i] = createEmptyNode()
	}
	createEdges(nodes[3], nodes[1])
	createEdges(nodes[1], nodes[2])
	createEdges(nodes[2], nodes[0])
	createEdges(nodes[0], nodes[4])

	// Create Expected graph
	Expected, Keep := createExpectedNodes(nodes[3], nodes[4])
	createExpectedEdges(Expected[0], Expected[1])
	makeExpectedEdgeResidual(Expected[0], Expected[1])
	return TrimTreeTestCase{
		Initial:  graph,
		Expected: Expected,
		Keep:     Keep,
	}
}

// If we trim an empty graph it should still be empty afterwards
func createTestCase3() TrimTreeTestCase {
	graph := &Graph{make(Nodes, 0)}
	Expected, Keep := createExpectedNodes()
	return TrimTreeTestCase{
		Initial:  graph,
		Expected: Expected,
		Keep:     Keep,
	}
}

// This test case looks like:
//   0
//
// After Keeping 0. We should see:
//   0
func createTestCase4() TrimTreeTestCase {
	graph := &Graph{make(Nodes, 1)}
	nodes := graph.Nodes
	for i := range nodes {
		nodes[i] = createEmptyNode()
	}
	Expected, Keep := createExpectedNodes(nodes[0])
	return TrimTreeTestCase{
		Initial:  graph,
		Expected: Expected,
		Keep:     Keep,
	}
}

func createTrimTreeTestCases() []TrimTreeTestCase {
	caseGenerators := []func() TrimTreeTestCase{
		createTestCase1,
		createTestCase2,
		createTestCase3,
		createTestCase4,
	}
	cases := make([]TrimTreeTestCase, len(caseGenerators))
	for i, gen := range caseGenerators {
		cases[i] = gen()
	}
	return cases
}

func TestTrimTree(t *testing.T) {
	tests := createTrimTreeTestCases()
	for _, test := range tests {
		graph := test.Initial
		graph.TrimTree(test.Keep)
		if !graphsEqual(graph, test.Expected) {
			t.Fatalf("Graphs do not match.\nExpected: %s\nFound: %s\n",
				expectedNodesDebugString(test.Expected),
				graphDebugString(graph))
		}
	}
}
