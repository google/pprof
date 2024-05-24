Want to contribute? Great: read the page (including the small print at the end).

# Before you contribute

As an individual, sign the [Google Individual Contributor License
Agreement](https://cla.developers.google.com/about/google-individual) (CLA)
online. This is required for any of your code to be accepted.

Before you start working on a larger contribution, get in touch with us first
through the issue tracker with your idea so that we can help out and possibly
guide you. Coordinating up front makes it much easier to avoid frustration later
on.

# What to expect

All submissions (including by project members) are done via GitHub pull requests
and require a code review by a project member.

We expect contributions to be good, clean code following style and practices for
the language the contribution is in. The pprof source code is in Go with a bit
of JavaScript, CSS and HTML. If you are new to Go, read [Effective
Go](https://golang.org/doc/effective_go.html) and the [summary on typical
comments during Go code
reviews](https://github.com/golang/go/wiki/CodeReviewComments).

All contributions should include automated tests for the change. We are
continuously improving pprof automated testing and we can't accept changes that
are not helping that direction. Code coverage numbers are automatically
published in each pull request - we expect that number to go up.  Note that
adding a good test often requires more time than the fix itself - this is
expected and you should be prepared for that time investment.

Contributions that do not meet the above guidelines will get less attention and
will be slow to get accepted or won't be accepted at all. We will also likely
refuse to accept changes that have fairly limited audience but will require us
to commit to maintain them for foreseeable future. This includes support for
specific platforms, making internal pprof APIs public, etc.

# Development

The commands below assume `/tmp/pprof` as the location for the source code.
You can change it to a directory of your choice.

To get the source code, run

```
cd /tmp
git clone git@github.com:google/pprof.git
cd pprof
```

To run the tests, do

```
cd /tmp/pprof
go test -v ./...
(cd browsertests && go test)
```

When you wish to work with your own fork of the source (which is required to be
able to create a pull request), you'll want to get your fork repo as another Git
remote in the same `github.com/google/pprof` directory. Otherwise, if you'll `go
get` your fork directly, you'll be getting errors like `use of internal package
not allowed` when running tests.  To set up the remote do something like

```
cd /tmp/pprof
git remote add aalexand git@github.com:aalexand/pprof.git
git fetch aalexand
git checkout -b my-new-feature
# hack hack hack
go test -v ./...
(cd browsertests && go test)
git commit -a -m "Add new feature."
git push aalexand
```

where `aalexand` is your GitHub user ID. Then proceed to the GitHub UI to send a
code review.

# The small print

Contributions made by corporations are covered by a different agreement than the
one above, the [Software Grant and Corporate Contributor License
Agreement](https://cla.developers.google.com/about/google-corporate).
