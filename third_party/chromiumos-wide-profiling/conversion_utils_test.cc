// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "chromiumos-wide-profiling/compat/string.h"
#include "chromiumos-wide-profiling/compat/test.h"
#include "chromiumos-wide-profiling/conversion_utils.h"
#include "chromiumos-wide-profiling/perf_test_files.h"
#include "chromiumos-wide-profiling/scoped_temp_path.h"
#include "chromiumos-wide-profiling/test_utils.h"

namespace quipper {

TEST(ConversionUtilsTest, TestTextOutput) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  string output_path = output_dir.path();

  for (const char* test_file : perf_test_files::GetPerfDataFiles()) {
    FormatAndFile input, output;

    input.filename = GetTestInputFilePath(test_file);
    input.format = kPerfFormat;
    output.filename = output_path + test_file + ".pb_text";
    output.format = kProtoTextFormat;
    EXPECT_TRUE(ConvertFile(input, output));

    string golden_file = GetTestInputFilePath(string(test_file) + ".pb_text");
    LOG(INFO) << "golden: " << golden_file;
    LOG(INFO) << "output: " << output.filename;
    EXPECT_TRUE(CompareFileContents(golden_file, output.filename));
  }
}

}  // namespace quipper
