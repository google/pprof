module github.com/google/pprof/browsertests

go 1.26.0

// Use the version of pprof in this directory tree.
replace github.com/google/pprof => ../

require (
	github.com/chromedp/chromedp v0.20.1
	github.com/google/pprof v0.0.0
)

require (
	github.com/chromedp/cdproto v0.157.8 // indirect
	github.com/go-json-experiment/json v0.0.0-20260213210345-44df1a37e875 // indirect
	github.com/ianlancetaylor/demangle v0.0.0-20250417193237-f615e6bd150b // indirect
)
