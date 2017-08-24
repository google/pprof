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

import "html/template"

// webTemplate defines a collection of related templates:
//    css
//    header
//    script
//    graph
//    top
var webTemplate = template.Must(template.New("web").Parse(`
{{define "css"}}
<style type="text/css">
html, body {
  height: 100%;
  min-height: 100%;
  margin: 0px;
}
body {
  width: 100%;
  height: 100%;
  min-height: 100%;
  overflow: hidden;
}
#graphcontainer {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 100%;
  width: 100%;
  min-width: 100%;
  margin: 0px;
}
#graph {
  flex: 1 1 auto;
  overflow: hidden;
}
svg {
  width: 100%;
  height: auto;
}
button {
  margin-top: 5px;
  margin-bottom: 5px;
}
#detailtext {
  display: none;
  position: fixed;
  top: 20px;
  right: 10px;
  background-color: #ffffff;
  min-width: 160px;
  border: 1px solid #888;
  box-shadow: 4px 4px 4px 0px rgba(0,0,0,0.2);
  z-index: 1;
}
#closedetails {
  float: right;
  margin: 2px;
}
#home {
  font-size: 14pt;
  padding-left: 0.5em;
  padding-right: 0.5em;
  float: right;
}
.menubar {
  display: inline-block;
  background-color: #f8f8f8;
  border: 1px solid #ccc;
  width: 100%;
}
.menu-header {
  position: relative;
  display: inline-block;
  padding: 2px 2px;
  cursor: default;
  font-size: 14pt;
}
.menu {
  display: none;
  position: absolute;
  background-color: #f8f8f8;
  border: 1px solid #888;
  box-shadow: 4px 4px 4px 0px rgba(0,0,0,0.2);
  z-index: 1;
  margin-top: 2px;
  left: 0px;
  min-width: 5em;
}
.menu hr {
  background-color: #fff;
  margin-top: 0px;
  margin-bottom: 0px;
}
.menu button {
  display: block;
  width: 100%;
  margin: 0px;
  text-align: left;
  padding-left: 2px;
  background-color: #fff;
  font-size: 12pt;
  border: none;
}
.menu-header:hover {
  background-color: #ccc;
}
.menu-header:hover .menu {
  display: block;
}
.menu button:hover {
  background-color: #ccc;
}
#searchbox {
  margin-left: 10pt;
}
#topcontainer {
  width: 100%;
  height: 100%;
  max-height: 100%;
  overflow: scroll;
}
#toptable {
  border-spacing: 0px;
  width: 100%;
}
#toptable tr th {
  border-bottom: 1px solid black;
  text-align: right;
  padding-left: 1em;
}
#toptable tr th:nth-child(6) { text-align: left; }
#toptable tr th:nth-child(7) { text-align: left; }
#toptable tr td {
  padding-left: 1em;
  font: monospace;
  text-align: right;
  white-space: nowrap;
  cursor: default;
}
#toptable tr td:nth-child(6) {
  text-align: left;
  max-width: 30em;  // Truncate very long names
  overflow: hidden;
}
#toptable tr td:nth-child(7) { text-align: left; }
.hilite {
  background-color: #ccf;
}
</style>
{{end}}

{{define "header"}}
<div id="detailtext">
<button id="closedetails">Close</button>
{{range .Legend}}<div>{{.}}</div>{{end}}
</div>

<div class="menubar">

<div class="menu-header">
View
<div class="menu">
{{if (ne .Type "top")}}
  <button title="{{.Help.top}}" id="topbtn">Top</button>
{{end}}
{{if (ne .Type "dot")}}
  <button title="{{.Help.graph}}" id="graphbtn">Graph</button>
{{end}}
<hr>
<button title="{{.Help.details}}" id="details">Details</button>
</div>
</div>

<div class="menu-header">
Functions
<div class="menu">
<button title="{{.Help.peek}}" id="peek">Peek</button>
<button title="{{.Help.list}}" id="list">List</button>
<button title="{{.Help.disasm}}" id="disasm">Disassemble</button>
</div>
</div>

<div class="menu-header">
Refine
<div class="menu">
<button title="{{.Help.focus}}" id="focus">Focus</button>
<button title="{{.Help.ignore}}" id="ignore">Ignore</button>
<button title="{{.Help.hide}}" id="hide">Hide</button>
<button title="{{.Help.show}}" id="show">Show</button>
<hr>
<button title="{{.Help.reset}}" id="reset">Reset</button>
</div>
</div>

<input id="searchbox" type="text" placeholder="Search regexp" autocomplete="off" autocapitalize="none" size=40>

<span id="home">{{.Title}}</span>

</div> <!-- menubar -->

<div id="errors">{{range .Errors}}<div>{{.}}</div>{{end}}</div>
{{end}}

{{define "graph" -}}
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{{.Title}}</title>
{{template "css" .}}
</head>
<body>

{{template "header" .}}
<div id="graphcontainer">
<div id="graph">
{{.Svg}}
</div>

</div>
{{template "script" .}}
<script>viewer({{.BaseURL}}, {{.Nodes}})</script>
</body>
</html>
{{end}}

{{define "script"}}
<script>
// Make svg pannable and zoomable.
// Call clickHandler(t) if a click event is caught by the pan event handlers.
function initPanAndZoom(svg, clickHandler) {
  'use strict';

  // Current mouse/touch handling mode
  const IDLE = 0
  const MOUSEPAN = 1
  const TOUCHPAN = 2
  const TOUCHZOOM = 3
  let mode = IDLE

  // State needed to implement zooming.
  let currentScale = 1.0
  const initWidth = svg.viewBox.baseVal.width
  const initHeight = svg.viewBox.baseVal.height

  // State needed to implement panning.
  let panLastX = 0      // Last event X coordinate
  let panLastY = 0      // Last event Y coordinate
  let moved = false     // Have we seen significant movement
  let touchid = null    // Current touch identifier

  // State needed for pinch zooming
  let touchid2 = null     // Second id for pinch zooming
  let initGap = 1.0       // Starting gap between two touches
  let initScale = 1.0     // currentScale when pinch zoom started
  let centerPoint = null  // Center point for scaling

  // Convert event coordinates to svg coordinates.
  function toSvg(x, y) {
    const p = svg.createSVGPoint()
    p.x = x
    p.y = y
    let m = svg.getCTM()
    if (m == null) m = svg.getScreenCTM()  // Firefox workaround.
    return p.matrixTransform(m.inverse())
  }

  // Change the scaling for the svg to s, keeping the point denoted
  // by u (in svg coordinates]) fixed at the same screen location.
  function rescale(s, u) {
    // Limit to a good range.
    if (s < 0.2) s = 0.2
    if (s > 10.0) s = 10.0

    currentScale = s

    // svg.viewBox defines the visible portion of the user coordinate
    // system.  So to magnify by s, divide the visible portion by s,
    // which will then be stretched to fit the viewport.
    const vb = svg.viewBox
    const w1 = vb.baseVal.width
    const w2 = initWidth / s
    const h1 = vb.baseVal.height
    const h2 = initHeight / s
    vb.baseVal.width = w2
    vb.baseVal.height = h2

    // We also want to adjust vb.baseVal.x so that u.x remains at same
    // screen X coordinate.  In other words, want to change it from x1 to x2
    // so that:
    //     (u.x - x1) / w1 = (u.x - x2) / w2
    // Simplifying that, we get
    //     (u.x - x1) * (w2 / w1) = u.x - x2
    //     x2 = u.x - (u.x - x1) * (w2 / w1)
    vb.baseVal.x = u.x - (u.x - vb.baseVal.x) * (w2 / w1)
    vb.baseVal.y = u.y - (u.y - vb.baseVal.y) * (h2 / h1)
  }

  function handleWheel(e) {
    if (e.deltaY == 0) return
    // Change scale factor by 1.1 or 1/1.1
    rescale(currentScale * (e.deltaY < 0 ? 1.1 : (1/1.1)),
            toSvg(e.offsetX, e.offsetY))
  }

  function setMode(m) {
    mode = m
    touchid = null
    touchid2 = null
  }

  function panStart(x, y) {
    moved = false
    panLastX = x
    panLastY = y
  }

  function panMove(x, y) {
    let dx = x - panLastX
    let dy = y - panLastY
    if (Math.abs(dx) <= 2 && Math.abs(dy) <= 2) return  // Ignore tiny moves

    moved = true
    panLastX = x
    panLastY = y

    // Firefox workaround: get dimensions from parentNode.
    const swidth = svg.clientWidth || svg.parentNode.clientWidth
    const sheight = svg.clientHeight || svg.parentNode.clientHeight

    // Convert deltas from screen space to svg space.
    dx *= (svg.viewBox.baseVal.width / swidth)
    dy *= (svg.viewBox.baseVal.height / sheight)

    svg.viewBox.baseVal.x -= dx
    svg.viewBox.baseVal.y -= dy
  }

  function handleScanStart(e) {
    if (e.button != 0) return  // Do not catch right-clicks etc.
    setMode(MOUSEPAN)
    panStart(e.clientX, e.clientY)
    e.preventDefault()
    svg.addEventListener("mousemove", handleScanMove)
  }

  function handleScanMove(e) {
    if (e.buttons == 0) {
      // Missed an end event, perhaps because mouse moved outside window.
      setMode(IDLE)
      svg.removeEventListener("mousemove", handleScanMove)
      return
    }
    if (mode == MOUSEPAN) panMove(e.clientX, e.clientY)
  }

  function handleScanEnd(e) {
    if (mode == MOUSEPAN) panMove(e.clientX, e.clientY)
    setMode(IDLE)
    svg.removeEventListener("mousemove", handleScanMove)
    if (!moved) clickHandler(e.target)
  }

  // Find touch object with specified identifier.
  function findTouch(tlist, id) {
    for (const t of tlist) {
      if (t.identifier == id) return t
    }
    return null
  }

 // Return distance between two touch points
  function touchGap(t1, t2) {
    const dx = t1.clientX - t2.clientX
    const dy = t1.clientY - t2.clientY
    return Math.hypot(dx, dy)
  }

  function handleTouchStart(e) {
    if (mode == IDLE && e.changedTouches.length == 1) {
      // Start touch based panning
      const t = e.changedTouches[0]
      setMode(TOUCHPAN)
      touchid = t.identifier
      panStart(t.clientX, t.clientY)
      e.preventDefault()
    } else if (mode == TOUCHPAN && e.touches.length == 2) {
      // Start pinch zooming
      setMode(TOUCHZOOM)
      const t1 = e.touches[0]
      const t2 = e.touches[1]
      touchid = t1.identifier
      touchid2 = t2.identifier
      initScale = currentScale
      initGap = touchGap(t1, t2)
      centerPoint = toSvg((t1.clientX + t2.clientX) / 2,
                          (t1.clientY + t2.clientY) / 2)
      e.preventDefault()
    }
  }

  function handleTouchMove(e) {
    if (mode == TOUCHPAN) {
      const t = findTouch(e.changedTouches, touchid)
      if (t == null) return
      if (e.touches.length != 1) {
        setMode(IDLE)
        return
      }
      panMove(t.clientX, t.clientY)
      e.preventDefault()
    } else if (mode == TOUCHZOOM) {
      // Get two touches; new gap; rescale to ratio.
      const t1 = findTouch(e.touches, touchid)
      const t2 = findTouch(e.touches, touchid2)
      if (t1 == null || t2 == null) return
      const gap = touchGap(t1, t2)
      rescale(initScale * gap / initGap, centerPoint)
      e.preventDefault()
    }
  }

  function handleTouchEnd(e) {
    if (mode == TOUCHPAN) {
      const t = findTouch(e.changedTouches, touchid)
      if (t == null) return
      panMove(t.clientX, t.clientY)
      setMode(IDLE)
      e.preventDefault()
      if (!moved) clickHandler(t.target)
    } else if (mode == TOUCHZOOM) {
      setMode(IDLE)
      e.preventDefault()
    }
  }

  svg.addEventListener("mousedown", handleScanStart)
  svg.addEventListener("mouseup", handleScanEnd)
  svg.addEventListener("touchstart", handleTouchStart)
  svg.addEventListener("touchmove", handleTouchMove)
  svg.addEventListener("touchend", handleTouchEnd)
  svg.addEventListener("wheel", handleWheel, true)
}

function viewer(baseUrl, nodes) {
  'use strict';

  // Elements
  const search = document.getElementById("searchbox")
  const graph0 = document.getElementById("graph0")
  const svg = (graph0 == null ? null : graph0.parentElement)
  const toptable = document.getElementById("toptable")

  let regexpActive = false
  let selected = new Map()
  let origFill = new Map()
  let searchAlarm = null
  let buttonsEnabled = true

  function handleDetails() {
    const detailsText = document.getElementById("detailtext")
    if (detailsText != null) detailsText.style.display = "block"
  }

  function handleCloseDetails() {
    const detailsText = document.getElementById("detailtext")
    if (detailsText != null) detailsText.style.display = "none"
  }

  function handleReset() { window.location.href = baseUrl }
  function handleTop() { navigate("/top", "f", false) }
  function handleGraph() { navigate("/", "f", false) }
  function handleList() { navigate("/weblist", "f", true) }
  function handleDisasm() { navigate("/disasm", "f", true) }
  function handlePeek() { navigate("/peek", "f", true) }
  function handleFocus() { navigate(baseUrl, "f", false) }
  function handleShow() { navigate(baseUrl, "s", false) }
  function handleIgnore() { navigate(baseUrl, "i", false) }
  function handleHide() { navigate(baseUrl, "h", false) }

  function handleKey(e) {
    if (e.keyCode != 13) return
    handleFocus()
    e.preventDefault()
  }

  function handleSearch() {
    // Delay expensive processing so a flurry of key strokes is handled once.
    if (searchAlarm != null) {
      clearTimeout(searchAlarm)
    }
    searchAlarm = setTimeout(selectMatching, 300)

    regexpActive = true
    updateButtons()
  }

  function selectMatching() {
    searchAlarm = null
    let re = null
    if (search.value != "") {
      try {
        re = new RegExp(search.value)
      } catch (e) {
        // TODO: Display error state in search box
        return
      }
    }

    function match(text) {
      return re != null && re.test(text)
    }

    // drop currently selected items that do not match re.
    selected.forEach(function(v, n) {
      if (!match(nodes[n])) {
        unselect(n, document.getElementById("node" + n))
      }
    })

    // add matching items that are not currently selected.
    for (let n = 0; n < nodes.length; n++) {
      if (!selected.has(n) && match(nodes[n])) {
        select(n, document.getElementById("node" + n))
      }
    }

    updateButtons()
  }

  function toggleSvgSelect(elem) {
    // Walk up to immediate child of graph0
    while (elem != null && elem.parentElement != graph0) {
      elem = elem.parentElement
    }
    if (!elem) return

    // Disable regexp mode.
    regexpActive = false

    const n = nodeId(elem)
    if (n < 0) return
    if (selected.has(n)) {
      unselect(n, elem)
    } else {
      select(n, elem)
    }
    updateButtons()
  }

  function unselect(n, elem) {
    if (elem == null) return
    selected.delete(n)
    setBackground(elem, false)
  }

  function select(n, elem) {
    if (elem == null) return
    selected.set(n, true)
    setBackground(elem, true)
  }

  function nodeId(elem) {
    const id = elem.id
    if (!id) return -1
    if (!id.startsWith("node")) return -1
    const n = parseInt(id.slice(4), 10)
    if (isNaN(n)) return -1
    if (n < 0 || n >= nodes.length) return -1
    return n
  }

  function setBackground(elem, set) {
    // Handle table row highlighting.
    if (elem.nodeName == "TR") {
      elem.classList.toggle("hilite", set)
      return
    }

    // Handle svg element highlighting.
    const p = findPolygon(elem)
    if (p != null) {
      if (set) {
        origFill.set(p, p.style.fill)
        p.style.fill = "#ccccff"
      } else if (origFill.has(p)) {
        p.style.fill = origFill.get(p)
      }
    }
  }

  function findPolygon(elem) {
    if (elem.localName == "polygon") return elem
    for (const c of elem.children) {
      const p = findPolygon(c)
      if (p != null) return p
    }
    return null
  }

  // convert a string to a regexp that matches that string.
  function quotemeta(str) {
    return str.replace(/([\\\.?+*\[\](){}|^$])/g, '\\$1')
  }

  // Navigate to specified path with current selection reflected
  // in the named parameter.
  function navigate(path, param, newWindow) {
    // The selection can be in one of two modes: regexp-based or
    // list-based.  Construct regular expression depending on mode.
    let re = regexpActive ? search.value : ""
    if (!regexpActive) {
      selected.forEach(function(v, key) {
        if (re != "") re += "|"
        re += quotemeta(nodes[key])
      })
    }

    const url = new URL(window.location.href)
    url.pathname = path
    url.hash = ""

    if (re != "") {
      // For focus/show, forget old parameter.  For others, add to re.
      const params = url.searchParams
      if (param != "f" && param != "s" && params.has(param)) {
        const old = params.get(param)
        if (old != "") {
          re += "|" + old
        }
      }
      params.set(param, re)
    }

    if (newWindow) {
      window.open(url.toString(), "_blank")
    } else {
      window.location.href = url.toString()
    }
  }

  function handleTopClick(e) {
    // Walk back until we find TR and then get the Name column (index 5)
    let elem = e.target
    while (elem != null && elem.nodeName != "TR") {
      elem = elem.parentElement
    }
    if (elem == null || elem.children.length < 6) return

    e.preventDefault()
    const tr = elem
    const td = elem.children[5]
    if (td.nodeName != "TD") return
    const name = td.innerText
    const index = nodes.indexOf(name)
    if (index < 0) return

    // Disable regexp mode.
    regexpActive = false

    if (selected.has(index)) {
      unselect(index, elem)
    } else {
      select(index, elem)
    }
    updateButtons()
  }

  function updateButtons() {
    const enable = (search.value != "" || selected.size != 0)
    if (buttonsEnabled == enable) return
    buttonsEnabled = enable
    for (const id of ["peek", "list", "disasm", "focus", "ignore", "hide", "show"]) {
      const btn = document.getElementById(id)
      if (btn != null) {
        btn.disabled = !enable
      }
    }
  }

  // Initialize button states
  updateButtons()

  // Setup event handlers
  if (svg != null) {
    initPanAndZoom(svg, toggleSvgSelect)
  }
  if (toptable != null) {
    toptable.addEventListener("mousedown", handleTopClick)
    toptable.addEventListener("touchstart", handleTopClick)
  }

  // Bind action to button with specified id.
  function addAction(id, action) {
    const btn = document.getElementById(id)
    if (btn != null) {
      btn.addEventListener("click", action)
      btn.addEventListener("touchstart", action)
    }
  }

  addAction("details", handleDetails)
  addAction("closedetails", handleCloseDetails)
  addAction("topbtn", handleTop)
  addAction("graphbtn", handleGraph)
  addAction("reset", handleReset)
  addAction("peek", handlePeek)
  addAction("list", handleList)
  addAction("disasm", handleDisasm)
  addAction("focus", handleFocus)
  addAction("ignore", handleIgnore)
  addAction("hide", handleHide)
  addAction("show", handleShow)

  search.addEventListener("input", handleSearch)
  search.addEventListener("keydown", handleKey)
}
</script>
{{end}}

{{define "top" -}}
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{{.Title}}</title>
{{template "css" .}}
</head>
<body>

{{template "header" .}}

<div id="topcontainer">
<table id="toptable">
<tr><th>Flat<th>Flat%<th>Sum%<th>Cum<th>Cum%<th>Name<th>Inlined?</tr>
{{range $i,$e := .Top}}
  <tr id="node{{$i}}"><td>{{$e.Flat}}<td>{{$e.FlatPercent}}<td>{{$e.SumPercent}}<td>{{$e.Cum}}<td>{{$e.CumPercent}}<td>{{$e.Name}}<td>{{$e.InlineLabel}}</tr>
{{end}}
</table>
</div>

{{template "script" .}}
<script>viewer({{.BaseURL}}, {{.Nodes}})</script>
</body>
</html>
{{end}}
`))
