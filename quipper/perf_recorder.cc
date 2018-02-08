// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_recorder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include "binary_data_utils.h"
#include "compat/proto.h"
#include "compat/string.h"
#include "perf_option_parser.h"
#include "perf_parser.h"
#include "perf_protobuf_io.h"
#include "perf_stat_parser.h"
#include "run_command.h"
#include "scoped_temp_path.h"

namespace quipper {

namespace {

// Supported perf subcommands.
const char kPerfRecordCommand[] = "record";
const char kPerfStatCommand[] = "stat";
const char kPerfMemCommand[] = "mem";

// Reads a perf data file and converts it to a PerfDataProto, which is stored as
// a serialized string in |output_string|. Returns true on success.
bool ParsePerfDataFileToString(const string& filename, string* output_string) {
  // Now convert it into a protobuf.

  PerfParserOptions options;
  // Make sure to remap address for security reasons.
  options.do_remap = true;
  // Discard unused perf events to reduce the protobuf size.
  options.discard_unused_events = true;
  // Read buildids from the filesystem ourself.
  options.read_missing_buildids = true;
  // Resolve split huge pages mappings.
  options.deduce_huge_page_mappings = true;

  PerfDataProto perf_data;
  return SerializeFromFileWithOptions(filename, options, &perf_data) &&
         perf_data.SerializeToString(output_string);
}

// Reads a perf data file and converts it to a PerfStatProto, which is stored as
// a serialized string in |output_string|. Returns true on success.
bool ParsePerfStatFileToString(const string& filename,
                               const std::vector<string>& command_line_args,
                               string* output_string) {
  PerfStatProto perf_stat;
  if (!ParsePerfStatFileToProto(filename, &perf_stat)) {
    LOG(ERROR) << "Failed to parse PerfStatProto from " << filename;
    return false;
  }

  // Fill in the command line field of the protobuf.
  string command_line;
  for (size_t i = 0; i < command_line_args.size(); ++i) {
    const string& arg = command_line_args[i];
    // Strip the output file argument from the command line.
    if (arg == "-o") {
      ++i;
      continue;
    }
    command_line.append(arg + " ");
  }
  command_line.resize(command_line.size() - 1);
  perf_stat.mutable_command_line()->assign(command_line);

  return perf_stat.SerializeToString(output_string);
}

}  // namespace

PerfRecorder::PerfRecorder() : PerfRecorder({"/usr/bin/perf"}) {}

PerfRecorder::PerfRecorder(const std::vector<string>& perf_binary_command)
    : perf_binary_command_(perf_binary_command) {}

bool PerfRecorder::RunCommandAndGetSerializedOutput(
    const std::vector<string>& perf_args, const double time_sec,
    string* output_string) {
  if (!ValidatePerfCommandLine(perf_args)) {
    LOG(ERROR) << "Perf arguments are not safe to run";
    return false;
  }

  // ValidatePerfCommandLine should have checked perf_args[0] == "perf", and
  // that perf_args[1] is a supported sub-command (e.g. "record" or "stat").

  const string& perf_type = perf_args[1];

  if (perf_type != kPerfRecordCommand && perf_type != kPerfStatCommand &&
      perf_type != kPerfMemCommand) {
    LOG(ERROR) << "Unsupported perf subcommand: " << perf_type;
    return false;
  }

  ScopedTempFile output_file;

  // Assemble the full command line:
  // - Replace "perf" in |perf_args[0]| with |perf_binary_command_| to
  //   guarantee we're running a binary we believe we can trust.
  // - Add our own paramters.

  std::vector<string> full_perf_args(perf_binary_command_);
  full_perf_args.insert(full_perf_args.end(),
                        perf_args.begin() + 1,  // skip "perf"
                        perf_args.end());
  full_perf_args.insert(full_perf_args.end(), {"-o", output_file.path()});

  // The perf stat output parser requires raw data from verbose output.
  if (perf_type == kPerfStatCommand) full_perf_args.emplace_back("-v");

  // Append the sleep command to run perf for |time_sec| seconds.
  std::stringstream time_string;
  time_string << time_sec;
  full_perf_args.insert(full_perf_args.end(),
                        {"--", "sleep", time_string.str()});

  // The perf command writes the output to a file, so ignore stdout.
  int status = RunCommand(full_perf_args, nullptr);
  if (status != 0) {
    PLOG(ERROR) << "perf command failed with status: " << status << ", Error";
    return false;
  }

  if (perf_type == kPerfRecordCommand || perf_type == kPerfMemCommand)
    return ParsePerfDataFileToString(output_file.path(), output_string);

  // Otherwise, parse as perf stat output.
  return ParsePerfStatFileToString(output_file.path(), full_perf_args,
                                   output_string);
}

}  // namespace quipper
