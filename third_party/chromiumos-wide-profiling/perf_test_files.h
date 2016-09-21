// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_TEST_FILES_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_TEST_FILES_H_

#include <vector>

namespace perf_test_files {

const std::vector<const char*>& GetPerfDataFiles();
const std::vector<const char*>& GetPerfPipedDataFiles();
const std::vector<const char*>& GetCorruptedPerfPipedDataFiles();
const std::vector<const char*>& GetPerfDataProtoFiles();

}  // namespace perf_test_files

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_TEST_FILES_H_
