module github.com/google/pprof/browsertests

go 1.24.0

toolchain go1.24.9

// Use the version of pprof in this directory tree.
replace github.com/google/pprof => ../

require (
	github.com/chromedp/chromedp v0.13.6
	github.com/google/pprof v0.0.0
)

require (
	github.com/chromedp/cdproto v0.0.0-20250403032234-65de8f5d025b // indirect
	github.com/chromedp/sysutil v1.1.0 // indirect
	github.com/go-json-experiment/json v0.0.0-20250211171154-1ae217ad3535 // indirect
	github.com/gobwas/httphead v0.1.0 // indirect
	github.com/gobwas/pool v0.2.1 // indirect
	github.com/gobwas/ws v1.4.0 // indirect
	github.com/ianlancetaylor/demangle v0.0.0-20250417193237-f615e6bd150b // indirect
	golang.org/x/sys v0.32.0 // indirect
)
