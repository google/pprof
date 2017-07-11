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

var graphTemplate = template.Must(template.New("graph").Parse(
	`<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<title>{{.Title}}</title>
<style type="text/css">
html, body {
  height: 100%;
  min-height: 100%;
  margin: 0px;
}
body {
  width: 100%;
  overflow: hidden;
}
h1 {
  font-weight: normal;
  font-size: 24px;
  padding: 0em;
  margin-top: 5px;
  margin-bottom: 5px;
}
#page {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 100%;
  width: 100%;
  min-width: 100%;
  margin: 0px;
}
#header {
  flex: 0 0 auto;
  width: 100%;
}
#leftbuttons {
  float: left;
}
#rightbuttons {
  float: right;
  display: table-cell;
  vertical-align: middle;
}
#rightbuttons label {
  vertical-align: middle;
}
#scale {
  vertical-align: middle;
}
#graph {
  flex: 1 1 auto;
  overflow: hidden;
}
svg {
  width: 100%;
  height: auto;
  border: 1px solid black;
}
button {
  margin-top: 5px;
  margin-bottom: 5px;
}
#reset, #scale {
  margin-left: 10px;
}
#detailtext {
  display: none;
  position: absolute;
  background-color: #ffffff;
  min-width: 160px;
  xborder: 1px solid black;
  border-top: 1px solid black;
  box-shadow: 2px 2px 2px 0px #aaa;
  z-index: 1;
}
</style>
</head>
<body>
<h1>{{.Title}}</h1>
<div id="page">

<div id="header">
<div id="leftbuttons">
<button id="details">&#x25b7; Details</button>
<div id="detailtext">
{{range .Legend}}<div>{{.}}</div>{{end}}
</div>
<button id="list">List</button>
<button id="disasm">Disasm</button>
<input id="searchbox" type="text" placeholder="Search regexp" autocomplete="off" autocapitalize="none" size=40/>
<button id="focus">Focus</button>
<button id="show">Show</button>
<button id="hide">Hide</button>
<button id="ignore">Ignore</button>
<button id="reset">Reset</button>
</div>
<div id="rightbuttons">
</div>
</div>

<div id="graph">
{{.Svg}}
</div>

</div>
</body>
</html>
<script>

// Make svg pannable and zoomable.
// Call clickHandler(t) if a click event is caught by the pan event handlers.
function initPanAndZoom(svg, clickHandler) {
  // Current mouse/touch handling mode
  const IDLE = 0
  const MOUSEPAN = 1
  const TOUCHPAN = 2
  const TOUCHZOOM = 3
  var mode = IDLE

  // State needed to implement zooming.
  var currentScale = 1.0
  var initWidth = svg.viewBox.baseVal.width
  var initHeight = svg.viewBox.baseVal.height

  // State needed to implement panning.
  var panLastX = 0      // Last event X coordinate
  var panLastY = 0      // Last event Y coordinate
  var moved = false     // Have we seen significant movement
  var touchid = null;   // Current touch identifier

  // State needed for pinch zooming
  var touchid2 = null;    // Second id for pinch zooming
  var initGap = 1.0       // Starting gap between two touches
  var initScale = 1.0     // currentScale when pinch zoom started
  var centerPoint = null  // Center point for scaling

  // Convert event coordinates to svg coordinates.
  var toSvg = function(x, y) {
    var p = svg.createSVGPoint()
    p.x = x
    p.y = y
    return p.matrixTransform(svg.getCTM().inverse())
  }

  // Change the scaling for the svg to s, keeping the point denoted
  // by u (in svg coordinates]) fixed at the same screen location.
  var rescale = function(s, u) {
    // Limit to a good range.
    if (s < 0.2) s = 0.2
    if (s > 10.0) s = 10.0

    currentScale = s

    // svg.viewBox defines the visible portion of the user coordinate
    // system.  So to magnify by s, divide the visible portion by s,
    // which will then be stretched to fit the viewport.
    var vb = svg.viewBox
    var w1 = vb.baseVal.width
    var w2 = initWidth / s
    var h1 = vb.baseVal.height
    var h2 = initHeight / s
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

  var handleWheel = function(e) {
    if (e.deltaY == 0) return
    // Change scale factor by 1.1 or 1/1.1
    rescale(currentScale * (e.deltaY < 0 ? 1.1 : (1/1.1)),
            toSvg(e.offsetX, e.offsetY))
  }

  var setMode = function(m) {
    mode = m
    touchid = null
    touchid2 = null
  }

  var panStart = function(x, y) {
    moved = false
    panLastX = x
    panLastY = y
  }

  var panMove = function(x, y) {
    var dx = x - panLastX
    var dy = y - panLastY
    if (Math.abs(dx) <= 2 && Math.abs(dy) <= 2) return  // Ignore tiny moves

    moved = true
    panLastX = x
    panLastY = y

    // Convert deltas from screen space to svg space.
    dx *= (svg.viewBox.baseVal.width / svg.clientWidth)
    dy *= (svg.viewBox.baseVal.height / svg.clientHeight)

    svg.viewBox.baseVal.x -= dx
    svg.viewBox.baseVal.y -= dy
  }

  var handleScanStart = function(e) {
    if (e.button != 0) return  // Do not catch right-clicks etc.
    setMode(MOUSEPAN)
    panStart(e.screenX, e.screenY)
    e.preventDefault()
    svg.addEventListener("mousemove", handleScanMove)
  }

  var handleScanMove = function(e) {
    if (mode == MOUSEPAN) panMove(e.screenX, e.screenY)
  }

  var handleScanEnd = function(e) {
    if (mode == MOUSEPAN) panMove(e.screenX, e.screenY)
    setMode(IDLE)
    svg.removeEventListener("mousemove", handleScanMove)
    if (!moved) clickHandler(e.target)
  }

  // Find touch object with specified identifier.
  var findTouch = function(tlist, id) {
    for (var i = 0; i < tlist.length; i++) {
      var t = tlist[i]
      if (t.identifier == id) return t
    }
    return null
  }

 // Return distance between two touch points
  var touchGap = function(t1, t2) {
    var dx = t1.clientX - t2.clientX
    var dy = t1.clientY - t2.clientY
    return Math.sqrt(Math.pow(dx, 2) + Math.pow(dy, 2))
  }

  var handleTouchStart = function(e) {
    if (mode == IDLE && e.changedTouches.length == 1) {
      // Start touch based panning
      var t = e.changedTouches[0]
      setMode(TOUCHPAN)
      touchid = t.identifier
      panStart(t.screenX, t.screenY)
      e.preventDefault()
    } else if (mode == TOUCHPAN && e.touches.length == 2) {
      // Start pinch zooming
      setMode(TOUCHZOOM)
      var t1 = e.touches[0]
      var t2 = e.touches[1]
      touchid = t1.identifier
      touchid2 = t2.identifier
      initScale = currentScale
      initGap = touchGap(t1, t2)
      centerPoint = toSvg((t1.clientX + t2.clientX) / 2,
                          (t1.clientY + t2.clientY) / 2)
      e.preventDefault()
    }
  }

  var handleTouchMove = function(e) {
    if (mode == TOUCHPAN) {
      var t = findTouch(e.changedTouches, touchid)
      if (t == null) return
      if (e.touches.length != 1) {
        setMode(IDLE)
        return
      }
      panMove(t.screenX, t.screenY)
      e.preventDefault()
    } else if (mode == TOUCHZOOM) {
      // Get two touches; new gap; rescale to ratio.
      var t1 = findTouch(e.touches, touchid)
      var t2 = findTouch(e.touches, touchid2)
      if (t1 == null || t2 == null) return
      var gap = touchGap(t1, t2)
      rescale(initScale * gap / initGap, centerPoint)
      e.preventDefault()
    }
  }

  var handleTouchEnd = function(e) {
    if (mode == TOUCHPAN) {
      var t = findTouch(e.changedTouches, touchid)
      if (t == null) return
      panMove(t.screenX, t.screenY)
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

function dotviewer(nodes) {
  // Elements
  var detailsButton = document.getElementById("details")
  var detailsText = document.getElementById("detailtext")
  var listButton = document.getElementById("list")
  var disasmButton = document.getElementById("disasm")
  var resetButton = document.getElementById("reset")
  var focusButton = document.getElementById("focus")
  var showButton = document.getElementById("show")
  var ignoreButton = document.getElementById("ignore")
  var hideButton = document.getElementById("hide")
  var search = document.getElementById("searchbox")
  var graph0 = document.getElementById("graph0")
  var svg = graph0.parentElement

  var currentRe = ""
  var selected = new Map()
  var origFill = new Map()
  var searchAlarm = null
  var buttonsEnabled = true

  var handleDetails = function() {
    if (detailtext.style.display == "block") {
      detailtext.style.display = "none"
      detailsButton.innerText = "\u25b7 Details"
    } else {
      detailtext.style.display = "block"
      detailsButton.innerText = "\u25bd Details"
    }
  }

  var handleReset  = function() { window.location.href = "/" }
  var handleList   = function() { navigate("/weblist", "f", true) }
  var handleDisasm = function() { navigate("/disasm", "f", true) }
  var handleFocus  = function() { navigate("/", "f", false) }
  var handleShow   = function() { navigate("/", "s", false) }
  var handleIgnore = function() { navigate("/", "i", false) }
  var handleHide   = function() { navigate("/", "h", false) }

  var handleSearch = function() {
    // Delay processing so a flurry of key strokes is handled once.
    if (searchAlarm != null) {
      clearTimeout(searchAlarm)
    }
    searchAlarm = setTimeout(doSearch, 300)
  }

  doSearch = function() {
    searchAlarm = null
    var re = null
    if (search.value != "") {
      try {
        re = new RegExp(search.value)
      } catch (e) {
        // TODO: Display error state in search box
        return
      }
    }
    currentRe = search.value

    match = function(text) {
      return re != null && re.test(text)
    }

    // drop currently selected items that do not match re.
    selected.forEach(function(v, n) {
      if (!match(nodes[n])) {
        unselect(n, document.getElementById("node" + n))
      }
    })

    // add matching items that are not currently selected.
    for (var n = 0; n < nodes.length; n++) {
      if (!selected.has(n) && match(nodes[n])) {
        select(n, document.getElementById("node" + n))
      }
    }

    updateButtons()
  }

  var toggleSelect = function(elem) {
    // Walk up to immediate child of graph0
    while (elem != null && elem.parentElement != graph0) {
      elem = elem.parentElement
    }
    if (!elem) return

    // Disable regexp mode.
    currentRe = ""

    var n = nodeId(elem)
    if (n < 0) return
    if (selected.has(n)) {
      unselect(n, elem)
    } else {
      select(n, elem)
    }
    updateButtons()
  }

  unselect = function(n, elem) {
    if (elem == null) return
    selected.delete(n)
    setBackground(elem, false)
  }

  select = function(n, elem) {
    if (elem == null) return
    selected.set(n, true)
    setBackground(elem, true)
  }

  var nodeId = function(elem) {
    var id = elem.id
    if (!id) return -1
    if (!id.startsWith("node")) return -1
    var n = parseInt(id.slice(4), 10)
    if (isNaN(n)) return -1
    if (n < 0 || n >= nodes.length) return -1
    return n
  }

  var setBackground = function(elem, set) {
    var p = findPolygon(elem)
    if (p != null) {
      if (set) {
        origFill.set(p, p.style.fill)
        p.style.fill = "#ccccff"
      } else if (origFill.has(p)) {
        p.style.fill = origFill.get(p)
      }
    }
  }

  var findPolygon = function(elem) {
    if (elem.localName == "polygon") return elem
    for (var i = 0; i < elem.children.length; i++) {
      var p = findPolygon(elem.children[i])
      if (p != null) return p
    }
    return null
  }

  // Navigate to specified path with current selection reflected
  // in the named parameter.
  var navigate = function(path, param, newWindow) {
    // The selection can be in one of two modes: regexp-based or
    // list-based.  Construct regular expression depending on mode.
    var re = currentRe
    if (re == "") {
      selected.forEach(function(v, key) {
        if (re != "") re += "|"
        re += nodes[key]
      })
    }

    var url = new URL(window.location.href)
    url.pathname = path
    url.hash = ""

    if (re != "") {
      // For focus/show, forget old parameter.  For others, add to re.
      var params = url.searchParams
      if (param != "f" && param != "s" && params.has(param)) {
        var old = params.get(param)
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

  var updateButtons = function() {
    var enable = (currentRe != "" || selected.size != 0)
    if (buttonsEnabled == enable) return
    buttonsEnabled = enable
    var d = enable ? false : true
    listButton.disabled = d
    disasmButton.disabled = d
    focusButton.disabled = d
    showButton.disabled = d
    ignoreButton.disabled = d
    hideButton.disabled = d
  }

  // Initialize button states
  updateButtons()

  // Setup event handlers
  initPanAndZoom(svg, toggleSelect)
  
  var bindButtons = function(evt) {
    detailsButton.addEventListener(evt, handleDetails)
    listButton.addEventListener(evt, handleList)
    disasmButton.addEventListener(evt, handleDisasm)
    resetButton.addEventListener(evt, handleReset)
    focusButton.addEventListener(evt, handleFocus)
    showButton.addEventListener(evt, handleShow)
    ignoreButton.addEventListener(evt, handleIgnore)
    hideButton.addEventListener(evt, handleHide)
  }
  bindButtons("click")
  bindButtons("touchstart")
  search.addEventListener("input", handleSearch)
}

dotviewer({{.Nodes}})
</script>
`))
