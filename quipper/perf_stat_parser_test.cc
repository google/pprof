// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/logging.h"

#include "compat/string.h"
#include "compat/test.h"
#include "file_utils.h"
#include "perf_stat_parser.h"
#include "scoped_temp_path.h"

namespace quipper {

namespace {

const char kInvalidInput[] =
    "PerfDataProto\n"
    "Attr: Even Count BuildID\n"
    "1.234 1234.5 time seconds\n";

const char kSmallInput[] =
    "/uncore/reads/: 711983 1002113142 1002111143\n"
    "/uncore/writes/: 140867 1002113864 1002113864\n"
    "    \n";  // Test parsing an empty line

// From a Peppy running:
// 'perf stat -v -a -e cycles -e L1-dcache-loads -e bus-cycles -e r02c4 --'
// ' sleep 2'
const char kFullInput[] =
    "cycles: 19062079 4002390292 4002381587\n"
    "L1-dcache-loads: 2081375 4002517554 4002511235\n"
    "bus-cycles: 2259169 4002527446 4002523976\n"
    "r02c4: 201584 4002518485 4002518485\n"
    "\n"
    " Performance counter stats for 'system wide':\n"
    "\n"
    "          19062079      cycles                    [100.00%]\n"
    "           2081375      L1-dcache-loads           [100.00%]\n"
    "           2259169      bus-cycles                [100.00%]\n"
    "            201584      r02c4   \n"
    "\n"
    "       2.001402976 seconds time elapsed\n"
    "\n";

}  // namespace

TEST(PerfStatParserTest, InvalidStringReturnsFalse) {
  PerfStatProto proto;
  ASSERT_FALSE(ParsePerfStatOutputToProto(kInvalidInput, &proto));
}

TEST(PerfStatParserTest, ValidInputParsesCorrectly) {
  // Test string input
  PerfStatProto proto;
  ASSERT_TRUE(ParsePerfStatOutputToProto(kSmallInput, &proto));

  ASSERT_EQ(proto.line_size(), 2);

  const auto& line1 = proto.line(0);
  EXPECT_EQ("/uncore/reads/", line1.event_name());
  EXPECT_EQ(711983, line1.count());
  EXPECT_FALSE(line1.has_time_ms());

  const auto& line2 = proto.line(1);
  EXPECT_EQ("/uncore/writes/", line2.event_name());
  EXPECT_EQ(140867, line2.count());
  EXPECT_FALSE(line2.has_time_ms());

  // Test file input
  ScopedTempFile input;
  ASSERT_FALSE(input.path().empty());
  ASSERT_TRUE(BufferToFile(input.path(), string(kSmallInput)));
  PerfStatProto proto2;
  ASSERT_TRUE(ParsePerfStatFileToProto(input.path(), &proto2));

  ASSERT_EQ(proto2.line_size(), 2);

  const auto& line3 = proto2.line(0);
  EXPECT_EQ("/uncore/reads/", line3.event_name());
  EXPECT_EQ(711983, line3.count());
  EXPECT_FALSE(line3.has_time_ms());

  const auto& line4 = proto2.line(1);
  EXPECT_EQ("/uncore/writes/", line4.event_name());
  EXPECT_EQ(140867, line4.count());
  EXPECT_FALSE(line4.has_time_ms());
}

TEST(PerfStatParserTest, ValidFullStringParsesCorrectly) {
  PerfStatProto proto;
  ASSERT_TRUE(ParsePerfStatOutputToProto(kFullInput, &proto));

  ASSERT_EQ(proto.line_size(), 4);

  const auto& line1 = proto.line(0);
  EXPECT_EQ("cycles", line1.event_name());
  EXPECT_EQ(19062079, line1.count());
  EXPECT_EQ(2001, line1.time_ms());

  const auto& line2 = proto.line(1);
  EXPECT_EQ("L1-dcache-loads", line2.event_name());
  EXPECT_EQ(2081375, line2.count());
  EXPECT_EQ(2001, line2.time_ms());

  const auto& line3 = proto.line(2);
  EXPECT_EQ("bus-cycles", line3.event_name());
  EXPECT_EQ(2259169, line3.count());
  EXPECT_EQ(2001, line3.time_ms());

  const auto& line4 = proto.line(3);
  EXPECT_EQ("r02c4", line4.event_name());
  EXPECT_EQ(201584, line4.count());
  EXPECT_EQ(2001, line4.time_ms());
}

TEST(PerfStatParserTest, NonexistentFileReturnsFalse) {
  PerfStatProto proto;
  ASSERT_FALSE(ParsePerfStatFileToProto("/dev/null/nope/nope.txt", &proto));
}

TEST(PerfStatParserTest, ParseTime) {
  uint64_t out;
  EXPECT_TRUE(SecondsStringToMillisecondsUint64("123.456", &out));
  EXPECT_EQ(123456, out);
  EXPECT_TRUE(SecondsStringToMillisecondsUint64("2.0014", &out));
  EXPECT_EQ(2001, out);
  EXPECT_TRUE(SecondsStringToMillisecondsUint64("0.0027", &out));
  EXPECT_EQ(3, out);
  EXPECT_FALSE(SecondsStringToMillisecondsUint64("-10.0027", &out));
  EXPECT_FALSE(SecondsStringToMillisecondsUint64("string", &out));
  EXPECT_FALSE(SecondsStringToMillisecondsUint64("string.string", &out));
  EXPECT_FALSE(SecondsStringToMillisecondsUint64("23.string", &out));
  EXPECT_FALSE(SecondsStringToMillisecondsUint64("string.23456", &out));
  EXPECT_FALSE(SecondsStringToMillisecondsUint64("123.234.456", &out));
}

}  // namespace quipper
