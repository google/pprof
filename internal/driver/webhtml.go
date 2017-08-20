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
<meta charset="utf-8">
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
#page {
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
#reset {
  margin-left: 10px;
}
#detailtext {
  display: none;
  position: absolute;
  background-color: #ffffff;
  min-width: 160px;
  border-top: 1px solid black;
  box-shadow: 2px 2px 2px 0px #aaa;
  z-index: 1;
}
#actionbox {
  display: none;
  position: fixed;
  background-color: #ffffff;
  border: 1px solid black;
  box-shadow: 2px 2px 2px 0px #aaa;
  top: 20px;
  right: 20px;
  z-index: 1;
}
.actionhdr {
  background-color: #ddd;
  width: 100%;
  border-bottom: 1px solid black;
  border-top: 1px solid black;
  font-size: 14pt;
}
#actionbox > button {
  display: block;
  width: 100%;
  margin: 0px;
  text-align: left;
  padding-left: 0.5em;
  background-color: #fff;
  border: none;
  font-size: 12pt;
}
#actionbox > button:hover {
  background-color: #ddd;
}
#home {
  font-size: 20pt;
  padding-left: 0.5em;
  padding-right: 0.5em;
}
</style>
</head>
<body>

<button id="details">&#x25b7; Details</button>
<div id="detailtext">
{{range .Legend}}<div>{{.}}</div>{{end}}
</div>

<button id="reset">Reset</button>

<span id="home">{{.Title}}</span>

<input id="searchbox" type="text" placeholder="Search regexp" autocomplete="off" autocapitalize="none" size=40>

<div id="page">

<div id="errors">{{range .Errors}}<div>{{.}}</div>{{end}}</div>

<div id="graph">

<div id="actionbox">
<div class="actionhdr">Refine graph</div>
<button title="{{.Help.focus}}" id="focus">Focus</button>
<button title="{{.Help.ignore}}" id="ignore">Ignore</button>
<button title="{{.Help.hide}}" id="hide">Hide</button>
<button title="{{.Help.show}}" id="show">Show</button>
<div class="actionhdr">Show Functions</div>
<button title="{{.Help.peek}}" id="peek">Peek</button>
<button title="{{.Help.list}}" id="list">List</button>
<button title="{{.Help.disasm}}" id="disasm">Disassemble</button>
</div>

{{.Svg}}
</div>

</div>
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

function dotviewer(nodes) {
  'use strict';

  // Elements
  const detailsButton = document.getElementById("details")
  const detailsText = document.getElementById("detailtext")
  const actionBox = document.getElementById("actionbox")
  const listButton = document.getElementById("list")
  const disasmButton = document.getElementById("disasm")
  const resetButton = document.getElementById("reset")
  const peekButton = document.getElementById("peek")
  const focusButton = document.getElementById("focus")
  const showButton = document.getElementById("show")
  const ignoreButton = document.getElementById("ignore")
  const hideButton = document.getElementById("hide")
  const search = document.getElementById("searchbox")
  const graph0 = document.getElementById("graph0")
  const svg = graph0.parentElement

  let regexpActive = false
  let selected = new Map()
  let origFill = new Map()
  let searchAlarm = null
  let buttonsEnabled = true

  function handleDetails() {
    if (detailtext.style.display == "block") {
      detailtext.style.display = "none"
      detailsButton.innerText = "\u25b7 Details"
    } else {
      detailtext.style.display = "block"
      detailsButton.innerText = "\u25bd Details"
    }
  }

  function handleReset() { window.location.href = "/" }
  function handleList() { navigate("/weblist", "f", true) }
  function handleDisasm() { navigate("/disasm", "f", true) }
  function handlePeek() { navigate("/peek", "f", true) }
  function handleFocus() { navigate("/", "f", false) }
  function handleShow() { navigate("/", "s", false) }
  function handleIgnore() { navigate("/", "i", false) }
  function handleHide() { navigate("/", "h", false) }

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

  function toggleSelect(elem) {
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

  // Navigate to specified path with current selection reflected
  // in the named parameter.
  function navigate(path, param, newWindow) {
    // The selection can be in one of two modes: regexp-based or
    // list-based.  Construct regular expression depending on mode.
    let re = regexpActive ? search.value : ""
    if (!regexpActive) {
      selected.forEach(function(v, key) {
        if (re != "") re += "|"
        re += nodes[key]
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

  function updateButtons() {
    const enable = (search.value != "" || selected.size != 0)
    if (buttonsEnabled == enable) return
    buttonsEnabled = enable
    actionBox.style.display = enable ? "block" : "none"
  }

  // Initialize button states
  updateButtons()

  // Setup event handlers
  initPanAndZoom(svg, toggleSelect)
  
  function bindButtons(evt) {
    detailsButton.addEventListener(evt, handleDetails)
    resetButton.addEventListener(evt, handleReset)
    listButton.addEventListener(evt, handleList)
    disasmButton.addEventListener(evt, handleDisasm)
    peekButton.addEventListener(evt, handlePeek)
    focusButton.addEventListener(evt, handleFocus)
    showButton.addEventListener(evt, handleShow)
    ignoreButton.addEventListener(evt, handleIgnore)
    hideButton.addEventListener(evt, handleHide)
  }
  bindButtons("click")
  bindButtons("touchstart")
  search.addEventListener("input", handleSearch)
  search.addEventListener("keydown", handleKey)
}

dotviewer({{.Nodes}})
</script>
</body>
</html>
`))
