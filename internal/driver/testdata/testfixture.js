// TestFixture records log messages and errors in an array that will
// be returned to Go code. Each element in the result array is either
// an array of the form ["LOG", ...]", or ["ERROR", ...].
class TestFixture {
  constructor() {
    this.context = "";  // Added to front of all log and error messages.
    this.result = [];
  }

  run(name, subtest) {
    this.result.push(["LOG", "===", name]);
    this.context = "";
    subtest();
    this.context = "";
  }

  // setContext arranges to add name(args) to all messages added
  // until the next setContext or run call.
  setContext(name, ...args) {
    this.context = name + "(" + args.join(",") + ")"
  }

  log(...args) {
    this.result.push(["LOG", this.context, ...args]);
  }

  err(...args) {
    this.result.push(["ERROR", this.context, ...args]);
  }
};
