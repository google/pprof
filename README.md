# Introduction

pprof is a tool for visualization and analysis of profiling data.

pprof reads a collection of profiling samples in profile.proto format and
generates reports to visualize and help analyze the data. It can generate both
text and graphical reports (through the use of the dot visualization package).

profile.proto is a protocol buffer that describes a set of callstacks
and symbolization information. A common usage is to represent a set of
sampled callstacks from statistical profiling. The format is
described on the src/proto/profile.proto file. For details on protocol
buffers, see https://developers.google.com/protocol-buffers

Profiles can be read from a local file, or over http. Multiple
profiles of the same type can be aggregated or compared.

If the profile samples contain machine addresses, pprof can symbolize
them through the use of the native binutils tools (addr2line and nm).

**This is not an official Google product.**

# Building pprof

Prerequisites:

- Go development kit: https://golang.org/dl/
  Known to work with Go 1.5
- Graphviz: http://www.graphviz.org/
  Optional, used to generate graphic visualizations of profiles

To build it, run the COMPILE.sh script. The TEST.sh script runs unit tests.

# Basic usage

pprof can read a profile from a file or directly from a server via http.
Specify the profile input(s) in the command line, and use options to
indicate how to format the report.

## Generate a text report of the profile, sorted by hotness:

```
% pprof -top [main_binary] profile.pb.gz
Where
    main_binary:  Local path to the main program binary, to enable symbolization
    profile.pb.gz: Local path to the profile in a compressed protobuf, or
                   URL to the http service that serves a profile.
```

## Generate a graph in an SVG file, and open it with a web browser:

```
pprof -web [main_binary] profile.pb.gz
```

## Run pprof on interactive mode:

If no output formatting option is specified, pprof runs on interactive mode,
where reads the profile and accepts interactive commands for visualization and
refinement of the profile.

```
pprof [main_binary] profile.pb.gz

This will open a simple shell that takes pprof commands to generate reports.
Type 'help' for available commands/options.
```

## Further documentation

See doc/pprof.md for more detailed end-user documentation.

See doc/developer/pprof.dev.md for developer documentation.

See doc/developer/profile.proto.md for a description of the profile.proto format.
