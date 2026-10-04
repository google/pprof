Want to contribute? Great: read the page (including the small print at the end).

# Before you contribute

As an individual, sign the [Google Individual Contributor License
Agreement](https://cla.developers.google.com/about/google-individual) online.
All submissions to Google Open Source projects need to follow Google's
Contributor License Agreement (CLA), which covers any original work of
authorship included in the submission. This doesn't prohibit the use of coding
assistance tools, including tool-, AI-, or machine-generated code, as long as
these submissions abide by the CLA's requirements.

It is recommended that you first explicitly volunteer through the issue tracker
and get a "go-ahead" for the work before sending any pull requests. This will
make it less likely for your work to be declined.

# What to expect

The pprof source code is in Go with a bit of JavaScript, CSS and HTML. We expect
pull requests to be good, concise, clean code following style and practices for
the language the contribution is in.

Changes made with coding assistance tools, including AI, may be accepted as long
as they comply with the applicable Google CLA terms and the AI-integrated tool
is not listed as an author or co-author. It is important that the human author
carefully reviews the AI-assisted solution to make sure it is correct and
reasonably concise for the problem.

All contributions should include automated tests. Code coverage is automatically
published in each pull request - we expect coverage to go up (or at least not
decrease) in every pull request.

We expect most pull requests to be authored by active users of the tool.
Empirically, contributions that are not a solution to an acute problem someone
is having in their workflow tend to be lower quality and take longer to
converge, and are more likely to be rejected.

Contributions that do not meet the above guidelines will be slow to get accepted
or will be rejected. We will also likely reject changes that are low priority,
have limited audience, take too many feedback iterations to converge, or require
non-trivial maintenance (e.g., support for specific platforms, making internal
pprof APIs public).

# Development

The commands below assume `/tmp/pprof` as the location for the source code. You
can change it to a directory of your choice.

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
