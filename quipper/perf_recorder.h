// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_RECORDER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_RECORDER_H_

#include <string>
#include <vector>

#include "base/macros.h"

#include "compat/string.h"
#include "perf_reader.h"

namespace quipper {

class PerfRecorder {
 public:
  PerfRecorder();

  // Mostly for testing.
  // Security critical: No user-provided strings should be used!
  explicit PerfRecorder(const std::vector<string>& perf_binary_command);

  // Runs the perf command specified in |perf_args| for |time_sec| seconds. The
  // output is returned as a serialized protobuf in |output_string|. The
  // protobuf format depends on the provided perf command.
  bool RunCommandAndGetSerializedOutput(const std::vector<string>& perf_args,
                                        const double time_sec,
                                        string* output_string);

  // The command prefix for running perf. e.g., "perf", or "/usr/bin/perf",
  // or perhaps {"sudo", "/usr/bin/perf"}.
  const std::vector<string>& perf_binary_command() const {
    return perf_binary_command_;
  }

 private:
  const std::vector<string> perf_binary_command_;

  DISALLOW_COPY_AND_ASSIGN(PerfRecorder);
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_RECORDER_H_
