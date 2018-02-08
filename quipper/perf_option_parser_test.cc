// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_option_parser.h"

#include "compat/string.h"
#include "compat/test.h"

namespace quipper {

TEST(PerfOptionParserTest, GoodRecord) {
  EXPECT_TRUE(ValidatePerfCommandLine({"perf", "record"}));
  EXPECT_TRUE(ValidatePerfCommandLine({"perf", "record", "-e", "cycles"}));
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "record", "-e", "-$;(*^:,.Non-sense!"}));  // let perf reject it.
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "record", "-a", "-e", "iTLB-misses", "-c", "1000003"}));
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "record", "-a", "-e", "cycles", "-g", "-c", "4000037"}));
  EXPECT_TRUE(ValidatePerfCommandLine({"perf", "record", "-a", "-e", "cycles",
                                       "-j", "any_call", "-c", "1000003"}));
}

TEST(PerfOptionParserTest, GoodStat) {
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "stat", "-a", "-e", "cpu/mem-loads/", "-e", "cpu/mem-stores/"}));
}

// Options that control the output format should only be specified by quipper.
TEST(PerfOptionParserTest, BadRecord_OutputOptions) {
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-e", "cycles", "-v"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "--verbose", "-e", "cycles"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-q", "-e", "cycles"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-e", "cycles", "--quiet"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-e", "cycles", "-m", "512"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "record", "-e", "cycles", "--mmap-pages", "512"}));
}

TEST(PerfOptionParserTest, BadRecord_BannedOptions) {
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-e", "cycles", "-D"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-e", "cycles", "-D", "10"}));
}

TEST(PerfOptionParserTest, GoodMemRecord) {
  EXPECT_TRUE(ValidatePerfCommandLine({"perf", "mem", "record"}));
  EXPECT_TRUE(
      ValidatePerfCommandLine({"perf", "mem", "record", "-e", "cycles"}));
  // let perf reject it.
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "mem", "record", "-e", "-$;(*^:,.Non-sense!"}));
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "mem", "record", "-a", "-e", "iTLB-misses", "-c", "1000003"}));
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "mem", "record", "-a", "-e", "cycles", "-g", "-c", "4000037"}));
  EXPECT_TRUE(
      ValidatePerfCommandLine({"perf", "mem", "record", "-a", "-e", "cycles",
                               "-j", "any_call", "-c", "1000003"}));

  // Check perf-mem options that come before "record".
  // See http://man7.org/linux/man-pages/man1/perf-mem.1.html
  EXPECT_TRUE(ValidatePerfCommandLine(
      {"perf", "mem", "-t", "load", "record", "-e", "-$;(*^:,.Non-sense!"}));
  EXPECT_TRUE(
      ValidatePerfCommandLine({"perf", "mem", "--type", "load,store", "record",
                               "-a", "-e", "iTLB-misses", "-c", "1000003"}));
  EXPECT_TRUE(
      ValidatePerfCommandLine({"perf", "mem", "-D", "-x", ":", "record", "-a",
                               "-e", "cycles", "-g", "-c", "4000037"}));
  EXPECT_TRUE(
      ValidatePerfCommandLine({"perf", "mem", "-C", "0,1", "record", "-a", "-e",
                               "cycles", "-j", "any_call", "-c", "1000003"}));
}

TEST(PerfOptionParserTest, BadMemRecord_OutputOptions) {
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "-t", "load,store", "record", "-e", "cycles", "-v"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "-t", "load", "record", "--verbose", "-e", "cycles"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "-D", "-x", ":", "record", "-q", "-e", "cycles"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "-C", "0,1", "record", "-e", "cycles", "--quiet"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "record", "-e", "cycles", "-m", "512"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "record", "-e", "cycles", "--mmap-pages", "512"}));

  // Try some bad perf-mem options.
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "-y", "-z", "record", "-e", "-$;(*^:,.Non-sense!"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "mem", "--blah", "record", "-a",
                                        "-e", "iTLB-misses", "-c", "1000003"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "mem", "--no-way", "record", "-a", "-e",
                               "cycles", "-g", "-c", "4000037"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "mem", "--danger", "record", "-a", "-e",
                               "cycles", "-j", "any_call", "-c", "1000003"}));
}

TEST(PerfOptionParserTest, BadMemRecord_BannedOptions) {
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "mem", "record", "-e", "cycles", "-D"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "record", "-e", "cycles", "-D", "10"}));
}

// Options that control the output format should only be specified by quipper.
TEST(PerfOptionParserTest, BadStat_OutputOptions) {
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "stat", "-e", "cycles", "-v"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "stat", "--verbose", "-e", "cycles"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "stat", "-q", "-e", "cycles"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "stat", "-e", "cycles", "--quiet"}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "stat", "-e", "cycles", "-x", "::"}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "stat", "-e", "cycles", "--field-separator", ","}));
}

TEST(PerfOptionParserTest, BadStat_BannedOptions) {
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "stat", "--pre", "rm -rf /"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "stat", "--post", "rm -rf /"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "stat", "-d"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "stat", "--log-fd", "4"}));
}

TEST(PerfOptionParserTest, DontAllowOtherPerfSubcommands) {
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "list"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "report"}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "trace"}));
}

// Unsafe command lines for either perf command.
TEST(PerfOptionParserTest, Ugly) {
  for (const string &subcmd : {"record", "stat", "mem"}) {
    EXPECT_FALSE(ValidatePerfCommandLine({"perf", subcmd, "rm", "-rf", "/"}));
    EXPECT_FALSE(
        ValidatePerfCommandLine({"perf", subcmd, "--", "rm", "-rf", "/"}));
    EXPECT_FALSE(ValidatePerfCommandLine(
        {"perf", subcmd, "-e", "cycles", "rm", "-rf", "/"}));
    EXPECT_FALSE(ValidatePerfCommandLine(
        {"perf", subcmd, "-e", "cycles", "-o", "/root/haha.perf.data"}));
  }
}

// Regression test for correct past-the-end iteration.
TEST(PerfOptionParserTest, ValueCommandAtEnd) {
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "record", "-c" /*missing value!*/}));
  EXPECT_FALSE(
      ValidatePerfCommandLine({"perf", "stat", "-e" /*missing value!*/}));
  EXPECT_FALSE(ValidatePerfCommandLine({"perf", "mem",
                                        "record"
                                        "-j" /*missing value!*/}));
  EXPECT_FALSE(ValidatePerfCommandLine(
      {"perf", "mem", "-t", "load", "record", "-e" /*missing value!*/}));
}

}  // namespace quipper
