// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_command.h"

#include <vector>

#include "compat/string.h"
#include "compat/test.h"

namespace quipper {

TEST(RunCommandTest, StoresStdout) {
  std::vector<char> output;
  EXPECT_EQ(0, RunCommand({"/bin/sh", "-c", "echo 'Hello, world!'"}, &output));
  string output_str(output.begin(), output.end());
  EXPECT_EQ("Hello, world!\n", output_str);
}

TEST(RunCommandTest, RunsFromPath) {
  std::vector<char> output;
  EXPECT_EQ(0, RunCommand({"sh", "-c", "echo 'Hello, world!'"}, &output));
  string output_str(output.begin(), output.end());
  EXPECT_EQ("Hello, world!\n", output_str);
}

TEST(RunCommandTest, LargeStdout) {
  std::vector<char> output;
  EXPECT_EQ(0,
            RunCommand({"dd", "if=/dev/zero", "bs=5", "count=4096"}, &output));
  EXPECT_EQ(5 * 4096, output.size());
  EXPECT_EQ('\0', output[0]);
  EXPECT_EQ('\0', output[1]);
  EXPECT_EQ('\0', *output.rbegin());
}

TEST(RunCommandTest, StdoutToDevnull) {
  EXPECT_EQ(0, RunCommand({"/bin/sh", "-c", "echo 'Hello, world!'"}, nullptr));
}

TEST(RunCommandTest, StderrIsNotStored) {
  std::vector<char> output;
  EXPECT_EQ(0,
            RunCommand({"/bin/sh", "-c", "echo 'Hello, void!' >&2"}, &output));
  EXPECT_EQ(0, output.size());
}

TEST(RunCommandTest, NoSuchExecutable) {
  std::vector<char> output;
  int ret = RunCommand({"/doesnt-exist/not-bin/true"}, &output);
  int save_errno = errno;
  EXPECT_EQ(-1, ret);
  EXPECT_EQ(ENOENT, save_errno);
}

}  // namespace quipper
