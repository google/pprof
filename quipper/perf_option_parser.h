// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_OPTION_PARSER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_OPTION_PARSER_H_

#include <vector>

#include "compat/string.h"

namespace quipper {

// Check that "args" is safe to pass to perf. That is, that it only contains
// safe options from a whitelist of those that modify event collection.
// e.g., "perf record -e cycles -- rm -rf /" is unsafe.
// This check should be made on the arguments coming from an untrusted caller of
// quipper before quipper adds its own arguments, like "-- sleep 2" to set the
// profile duration, or "-o /tmp/perf.data" to set the output path.
// This also requires that args[0] is "perf". Quipper can later substitute
// this with the One True perf binary.
// Returns |true| iff the command line is safe.
bool ValidatePerfCommandLine(const std::vector<string> &args);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_OPTION_PARSER_H_
