function TestFlame() {
  const PADDING = 2; // Matches PADDING in stackViewer

  const test = new TestFixture();
  const chart = document.getElementById("stack-chart");
  if (!chart) {
    test.err("could not find stack-chart");
    return;
  }
  const chartRect = chart.getBoundingClientRect();

  // Create map from box text to DOM element.
  // TODO: Generalize to support multiple boxes for a given piece of text.
  let boxMap;
  fetchBoxes();
  function fetchBoxes() {
    boxMap = new Map();
    const boxes = document.querySelectorAll(".boxbg");
    for (let box of boxes) {
      const text = box.innerText;
      boxMap.set(text, box);
    }
  }

  // rect gets the bounding box for box with text t.
  function rect(t) {
    const elem = boxMap.get(t);
    if (!elem) {
      test.err("did not find", t);
      return null;
    }
    return elem.getBoundingClientRect();
  }

  // checkCalls checks that box with text a is positioned w.r.t. box with
  // text b to indicate a call from a to b.  Expect a gap of the specified
  // number of rows.
  function checkCalls(a, b, gap = 0) {
    test.setContext("checkCalls", a, b, gap);
    const ra = rect(a);
    const rb = rect(b);
    if (!ra || !rb) return;

    const pixelGap = gap * rb.height;
    if (rb.top != ra.bottom + pixelGap) {
      test.err("not above");
    }
    // TODO: Allow checking boxes above pivots.
    if (rb.left < ra.left || rb.right > ra.right) {
      test.err("horizontal span of", a, "is not nested inside horizontal span of", b);
    }
  }

  // checkWidth checks that the width of the box with text t is approximately
  // the specified fraction of the total width.
  function checkWidth(t, fraction) {
    test.setContext("checkWidth", t, fraction);
    const r = rect(t);
    if (!r) return;
    const expect = (chartRect.width - 2*PADDING) * fraction;
    if (r.width < expect*0.95 || r.width > expect*1.05) {
      test.err("bad width", r.width, "expecting ~", expect);
    }
  }

  // Fake profile has the following stacks:
  //    100 F1 F2 F3
  //    200 F1 F2
  test.run("initial", function() {
    checkCalls("root", "F1");
    checkCalls("F1", "F2");
    checkCalls("F2", "F3");
    checkWidth("root", 300/300);
    checkWidth("F1", 300/300);
    checkWidth("F2", 300/300);
    checkWidth("F3", 100/300);
  });

  test.run("Pivot F3", function() {
    boxMap.get("F3").click();
    fetchBoxes();
    checkCalls("root", "F1");
    checkCalls("F1", "F2");
    checkCalls("F2", "F3", 1);
    checkWidth("root", 100/100);
    checkWidth("F1", 100/100);
    checkWidth("F2", 100/100);
    checkWidth("F3", 100/100);
  });

  test.run("NavigateWithoutPivot", function() {
    // Clear pivot
    boxMap.get("root").click();

    // Trigger link update.
    const btn = document.getElementById("graphbtn");
    if (!btn) {
      test.err("no graph button on page");
      return;
    }
    const event = new Event("mouseenter");
    btn.dispatchEvent(event);

    // Check that URL does not contain a focus parameter.
    test.log(btn.href);
    const url = new URL(btn.href);
    if (url.searchParams.has('f')) {
      test.err("unexpected focus parameter in URL", btn.href);
    }
  });

  return test.result;
}
TestFlame();
