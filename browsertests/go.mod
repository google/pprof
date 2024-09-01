module github.com/google/pprof/browsertests

go 1.22

// Use the version of pprof in this directory tree.
replace github.com/google/pprof => ../

require (
	github.com/chromedp/chromedp v0.9.2
	github.com/google/pprof v0.0.0
)

require (
	github.com/chromedp/cdproto v0.0.0-20230802225258-3cf4e6d46a89 // indirect
	github.com/chromedp/sysutil v1.0.0 // indirect
	github.com/gobwas/httphead v0.1.0 // indirect
	github.com/gobwas/pool v0.2.1 // indirect
	github.com/gobwas/ws v1.2.1 // indirect
	github.com/ianlancetaylor/demangle v0.0.0-20240312041847-bd984b5ce465 // indirect
	github.com/josharian/intern v1.0.0 // indirect
	github.com/mailru/easyjson v0.7.7 // indirect
	golang.org/x/sys v0.6.0 // indirect
)
