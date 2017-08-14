// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_STAT_PARSER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_STAT_PARSER_H_

#include <string>

#include "base/macros.h"

#include "compat/proto.h"
#include "compat/string.h"

namespace quipper {

// These functions parse the contents at |path| or of |data| into a
// PerfStatProto. Return true if at least one line of data was inserted into
// |proto|. Otherwise return false;
// Both functions below assume perf stat output is in the form:
//     "event: 123 123 123\n"
//     "event2: 123 123 123\n"
//     "..."
//     "1.234 seconds time elapsed"
bool ParsePerfStatFileToProto(const string& path, PerfStatProto* proto);
bool ParsePerfStatOutputToProto(const string& data, PerfStatProto* proto);

// This function assumes that |str| is of the form "1234.1234567" and returns
// false otherwise. This function does not accept negatives (e.g. "-12.23").
bool SecondsStringToMillisecondsUint64(const string& str, uint64_t* out);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_STAT_PARSER_H_
