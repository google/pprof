// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include "compat/string.h"
#include "compat/test.h"
#include "conversion_utils.h"
#include "perf_test_files.h"
#include "scoped_temp_path.h"
#include "test_utils.h"

namespace quipper {

class PerfFile : public ::testing::TestWithParam<const char*> {};

TEST_P(PerfFile, TextOutput) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  const string output_path = output_dir.path();

  const string test_file = GetParam();

  FormatAndFile input, output;

  input.filename = GetTestInputFilePath(test_file);
  input.format = kPerfFormat;
  output.filename = output_path + test_file + ".pb_text";
  output.format = kProtoTextFormat;
  EXPECT_TRUE(ConvertFile(input, output));

  string golden_file = GetTestInputFilePath(string(test_file) + ".pb_text");
  LOG(INFO) << "golden: " << golden_file;
  LOG(INFO) << "output: " << output.filename;

  CompareTextProtoFiles<PerfDataProto>(output.filename, golden_file);
}

INSTANTIATE_TEST_CASE_P(
    ConversionUtilsTest, PerfFile,
    ::testing::ValuesIn(perf_test_files::GetPerfDataFiles()));
}  // namespace quipper
