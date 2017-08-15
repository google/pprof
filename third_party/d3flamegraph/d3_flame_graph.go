// A D3.js plugin that produces flame graphs from hierarchical data.
// https://github.com/spiermar/d3-flame-graph
// Version 1.0.4
// See LICENSE file for license details

package d3flamegraph

// D3FlameGraphJS returns the d3.flameGraph.js file
const D3FlameGraphJS = `
// Copyright 2017 Martin Spier. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the “License”); you may not use this file except in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an “AS IS” BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

(function() {
  'use strict';

  /*jshint eqnull:true */
  // https://tc39.github.io/ecma262/#sec-array.prototype.find
  if (!Array.prototype.find) {
    Object.defineProperty(Array.prototype, 'find', {
      value: function(predicate) {
      // 1. Let O be ? ToObject(this value).
        if (this == null) {
          throw new TypeError('"this" is null or not defined');
        }

        var o = Object(this);

        // 2. Let len be ? ToLength(? Get(O, "length")).
        var len = o.length >>> 0;

        // 3. If IsCallable(predicate) is false, throw a TypeError exception.
        if (typeof predicate !== 'function') {
          throw new TypeError('predicate must be a function');
        }

        // 4. If thisArg was supplied, let T be thisArg; else let T be undefined.
        var thisArg = arguments[1];

        // 5. Let k be 0.
        var k = 0;

        // 6. Repeat, while k < len
        while (k < len) {
          // a. Let Pk be ! ToString(k).
          // b. Let kValue be ? Get(O, Pk).
          // c. Let testResult be ToBoolean(? Call(predicate, T, « kValue, k, O »)).
          // d. If testResult is true, return kValue.
          var kValue = o[k];
          if (predicate.call(thisArg, kValue, k, o)) {
            return kValue;
          }
          // e. Increase k by 1.
          k++;
        }

        // 7. Return undefined.
        return undefined;
      }
    });
  }
  /*jshint eqnull:false */

  function flameGraph() {

    var w = 960, // graph width
      h = null, // graph height
      c = 18, // cell height
      selection = null, // selection
      tooltip = true, // enable tooltip
      title = "", // graph title
      transitionDuration = 750,
      transitionEase = d3.easeCubic, // tooltip offset
      sort = true,
      reversed = false, // reverse the graph direction
      clickHandler = null;

    var tip = d3.tip()
      .direction("s")
      .offset([8, 0])
      .attr('class', 'd3-flame-graph-tip')
      .html(function(d) { return label(d); });

    var svg;

    var label = function(d) {
      return d.data.name + " (" + d3.format(".3f")(100 * (d.x1 - d.x0), 3) + "%, " + d.data.value + " samples)";
    };

    function setDetails(t) {
      var details = document.getElementById("details");
      if (details)
        details.innerHTML = t;
    }

    function name(d) {
      return d.data.name;
    }

    var colorMapper = function(d) {
      return d.highlight ? "#E600E6" : colorHash(d.data.name);
    };

    function generateHash(name) {
      // Return a vector (0.0->1.0) that is a hash of the input string.
      // The hash is computed to favor early characters over later ones, so
      // that strings with similar starts have similar vectors. Only the first
      // 6 characters are considered.
      var hash = 0, weight = 1, max_hash = 0, mod = 10, max_char = 6;
      if (name) {
        for (var i = 0; i < name.length; i++) {
          if (i > max_char) { break; }
          hash += weight * (name.charCodeAt(i) % mod);
          max_hash += weight * (mod - 1);
          weight *= 0.70;
        }
        if (max_hash > 0) { hash = hash / max_hash; }
      }
      return hash;
    }

    function colorHash(name) {
      // Return an rgb() color string that is a hash of the provided name,
      // and with a warm palette.
      var vector = 0;
      if (name) {
        var nameArr = name.split('` + "`" + `');
        if (nameArr.length > 1) {
          name = nameArr[nameArr.length -1]; // drop module name if present
        }
        name = name.split('(')[0]; // drop extra info
        vector = generateHash(name);
      }
      var r = 200 + Math.round(55 * vector);
      var g = 0 + Math.round(230 * (1 - vector));
      var b = 0 + Math.round(55 * (1 - vector));
      return "rgb(" + r + "," + g + "," + b + ")";
    }

    function hide(d) {
      if(!d.data.original) {
        d.data.original = d.data.value;
      }
      d.data.value = 0;
      if(d.children) {
        d.children.forEach(hide);
      }
    }

    function show(d) {
      d.data.fade = false;
      if(d.data.original) {
        d.data.value = d.data.original;
      }
      if(d.children) {
        d.children.forEach(show);
      }
    }

    function getSiblings(d) {
      var siblings = [];
      if (d.parent) {
        var me = d.parent.children.indexOf(d);
        siblings = d.parent.children.slice(0);
        siblings.splice(me, 1);
      }
      return siblings;
    }

    function hideSiblings(d) {
      var siblings = getSiblings(d);
      siblings.forEach(function(s) {
        hide(s);
      });
      if(d.parent) {
        hideSiblings(d.parent);
      }
    }

    function fadeAncestors(d) {
      if(d.parent) {
        d.parent.data.fade = true;
        fadeAncestors(d.parent);
      }
    }

    function getRoot(d) {
      if(d.parent) {
        return getRoot(d.parent);
      }
      return d;
    }

    function zoom(d) {
      tip.hide(d);
      hideSiblings(d);
      show(d);
      fadeAncestors(d);
      update();
      if (typeof clickHandler === 'function') {
        clickHandler(d);
      }
    }

    function searchTree(d, term) {
      var re = new RegExp(term),
          searchResults = [];

      function searchInner(d) {
        var label = d.data.name;

        if (d.children) {
          d.children.forEach(function (child) {
            searchInner(child);
          });
        }

        if (label.match(re)) {
          d.highlight = true;
          searchResults.push(d);
        } else {
          d.highlight = false;
        }
      }

      searchInner(d);
      return searchResults;
    }

    function clear(d) {
      d.highlight = false;
      if(d.children) {
        d.children.forEach(function(child) {
          clear(child);
        });
      }
    }

    function doSort(a, b) {
      if (typeof sort === 'function') {
        return sort(a, b);
      } else if (sort) {
        return d3.ascending(a.data.name, b.data.name);
      }
    }

    var partition = d3.partition();

    function update() {

      selection.each(function(root) {
        var x = d3.scaleLinear().range([0, w]),
            y = d3.scaleLinear().range([0, c]);

        if (sort) root.sort(doSort);
        root.sum(function(d) {
          if (d.fade) {
            return 0;
          }
          // The node's self value is its total value minus all children.
          var v = d.v || d.value || 0;
          if (d.children) {
            for (var i = 0; i < d.children.length; i++) {
              v -= d.children[i].value;
            }
          }
          return v;
        });
        partition(root);

        var kx = w / (root.x1 - root.x0);
        function width(d) { return (d.x1 - d.x0) * kx; }

        var g = d3.select(this).select("svg").selectAll("g").data(root.descendants());

        g.transition()
          .duration(transitionDuration)
          .ease(transitionEase)
          .attr("transform", function(d) { return "translate(" + x(d.x0) + "," + (reversed ? y(d.depth) : (h - y(d.depth) - c)) + ")"; });

        g.select("rect").transition()
          .duration(transitionDuration)
          .ease(transitionEase)
          .attr("width", width);

        var node = g.enter()
          .append("svg:g")
          .attr("transform", function(d) { return "translate(" + x(d.x0) + "," + (reversed ? y(d.depth) : (h - y(d.depth) - c)) + ")"; });

        node.append("svg:rect").attr("width", width);

        if (!tooltip)
          node.append("svg:title");

        node.append("foreignObject")
          .append("xhtml:div");

        // Now we have to re-select to see the new elements (why?).
        g = d3.select(this).select("svg").selectAll("g").data(root.descendants());

        g.attr("width", width)
          .attr("height", function(d) { return c; })
          .attr("name", function(d) { return d.data.name; })
          .attr("class", function(d) { return d.data.fade ? "frame fade" : "frame"; });

        g.select("rect")
          .attr("height", function(d) { return c; })
          .attr("fill", function(d) { return colorMapper(d); });

        if (!tooltip)
          g.select("title")
            .text(label);

        g.select("foreignObject")
          .attr("width", width)
          .attr("height", function(d) { return c; })
          .select("div")
          .attr("class", "label")
          .style("display", function(d) { return (width(d) < 35) ? "none" : "block";})
          .text(name);

        g.on('click', zoom);

        g.exit().remove();

        g.on('mouseover', function(d) {
          if (tooltip) tip.show(d);
          setDetails(label(d));
        }).on('mouseout', function(d) {
          if (tooltip) tip.hide(d);
          setDetails("");
        });
      });
    }

    function merge(data, samples) {
      samples.forEach(function (sample) {
        var node = data.find(function (element) {
          return element.name === sample.name;
        });

        if (node) {
          if (node.original) {
            node.original += sample.value;
          } else {
            node.value += sample.value;
          }
          if (sample.children) {
            if (!node.children) {
              node.children = [];
            }
            merge(node.children, sample.children);
          }
        } else {
          data.push(sample);
        }
      });
    }

    function chart(s) {
      var root = d3.hierarchy(s.datum(), function(d) { return d.c || d.children; });
      selection = s.datum(root);

      if (!arguments.length) return chart;

      if (!h) {
        h = (root.height + 2) * c;
      }

      selection.each(function(data) {

	      if (!svg) {
          svg = d3.select(this)
            .append("svg:svg")
            .attr("width", w)
            .attr("height", h)
            .attr("class", "partition d3-flame-graph")
            .call(tip);

          svg.append("svg:text")
            .attr("class", "title")
            .attr("text-anchor", "middle")
            .attr("y", "25")
            .attr("x", w/2)
            .attr("fill", "#808080")
            .text(title);
        }
      });

      // first draw
      update();
    }

    chart.height = function (_) {
      if (!arguments.length) { return h; }
      h = _;
      return chart;
    };

    chart.width = function (_) {
      if (!arguments.length) { return w; }
      w = _;
      return chart;
    };

    chart.cellHeight = function (_) {
      if (!arguments.length) { return c; }
      c = _;
      return chart;
    };

    chart.tooltip = function (_) {
      if (!arguments.length) { return tooltip; }
      if (typeof _ === "function") {
        tip = _;
      }
      tooltip = !!_;
      return chart;
    };

    chart.title = function (_) {
      if (!arguments.length) { return title; }
      title = _;
      return chart;
    };

    chart.transitionDuration = function (_) {
      if (!arguments.length) { return transitionDuration; }
      transitionDuration = _;
      return chart;
    };

    chart.transitionEase = function (_) {
      if (!arguments.length) { return transitionEase; }
      transitionEase = _;
      return chart;
    };

    chart.sort = function (_) {
      if (!arguments.length) { return sort; }
      sort = _;
      return chart;
    };

    chart.reversed = function (_) {
      if (!arguments.length) { return reversed; }
      reversed = _;
      return chart;
    };

    chart.label = function(_) {
      if (!arguments.length) { return label; }
      label = _;
      return chart;
    };

    chart.search = function(term) {
      var searchResults = [];
      selection.each(function(data) {
        searchResults = searchTree(data, term);
        update();
      });
      return searchResults;
    };

    chart.clear = function() {
      selection.each(function(data) {
        clear(data);
        update();
      });
    };

    chart.zoomTo = function(d) {
      zoom(d);
    };

    chart.resetZoom = function() {
      selection.each(function (data) {
        zoom(data); // zoom to root
      });
    };

    chart.onClick = function(_) {
      if (!arguments.length) {
        return clickHandler;
      }
      clickHandler = _;
      return chart;
    };
    
    chart.merge = function(samples) {
      var newRoot; // Need to re-create hierarchy after data changes.
      selection.each(function (root) {
        merge([root.data], [samples]);
        newRoot = d3.hierarchy(root.data, function(d) { return d.c || d.children; });
      });
      selection = selection.datum(newRoot);
      update();
    };
    
    chart.color = function(_) {
      if (!arguments.length) { return colorMapper; }
      colorMapper = _;
      return chart;
    };

    return chart;
  }

  if (typeof module !== 'undefined' && module.exports){
		module.exports = flameGraph;
	}
	else {
		d3.flameGraph = flameGraph;
	}
})();
`

// D3FlameGraphCSS returns the d3.flameGraph.CSS file
const D3FlameGraphCSS = `
.d3-flame-graph rect {
  stroke: #EEEEEE;
  fill-opacity: .8;
}

.d3-flame-graph rect:hover {
  stroke: #474747;
  stroke-width: 0.5;
  cursor: pointer;
}

.d3-flame-graph .label {
  pointer-events: none;
  white-space: nowrap;
  text-overflow: ellipsis;
  overflow: hidden;
  font-size: 12px;
  font-family: Verdana;
  margin-left: 4px;
  margin-right: 4px;
  line-height: 1.5;
  padding: 0 0 0;
  font-weight: 400;
  color: black;
  text-align: left;
}

.d3-flame-graph .fade {
  opacity: 0.6 !important;
}

.d3-flame-graph .title {
  font-size: 20px;
  font-family: Verdana;
}

.d3-flame-graph-tip {
  line-height: 1;
  font-family: Verdana;
  font-size: 12px;
  padding: 12px;
  background: rgba(0, 0, 0, 0.8);
  color: #fff;
  border-radius: 2px;
  pointer-events: none;
}

/* Creates a small triangle extender for the tooltip */
.d3-flame-graph-tip:after {
  box-sizing: border-box;
  display: inline;
  font-size: 10px;
  width: 100%;
  line-height: 1;
  color: rgba(0, 0, 0, 0.8);
  position: absolute;
  pointer-events: none;
}

/* Northward tooltips */
.d3-flame-graph-tip.n:after {
  content: "\25BC";
  margin: -1px 0 0 0;
  top: 100%;
  left: 0;
  text-align: center;
}

/* Eastward tooltips */
.d3-flame-graph-tip.e:after {
  content: "\25C0";
  margin: -4px 0 0 0;
  top: 50%;
  left: -8px;
}

/* Southward tooltips */
.d3-flame-graph-tip.s:after {
  content: "\25B2";
  margin: 0 0 1px 0;
  top: -8px;
  left: 0;
  text-align: center;
}

/* Westward tooltips */
.d3-flame-graph-tip.w:after {
  content: "\25B6";
  margin: -4px 0 0 -1px;
  top: 50%;
  left: 100%;
}
`
