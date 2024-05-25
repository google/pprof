Browser tests are separated out into a module of their own to avoid
polluting pprof dependencies with chromedp.

These tests can be run by executing the following in the top-level
of the pprof directory:

```shell
(cd browsertests && go test ./...)
```
