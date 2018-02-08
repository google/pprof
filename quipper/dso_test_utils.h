// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_DSO_TEST_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_DSO_TEST_UTILS_H_

#include <utility>
#include <vector>

#include "compat/string.h"

namespace quipper {
namespace testing {

void WriteElfWithBuildid(string filename, string section_name, string buildid);
// Note: an ELF with multiple buildid notes is unusual, but useful for testing.
void WriteElfWithMultipleBuildids(
    string filename,
    const std::vector<std::pair<string, string>> section_buildids);

}  // namespace testing
}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_DSO_TEST_UTILS_H_
