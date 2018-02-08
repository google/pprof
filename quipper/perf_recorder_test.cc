// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "compat/string.h"
#include "compat/test.h"
#include "perf_protobuf_io.h"
#include "perf_reader.h"
#include "perf_recorder.h"
#include "perf_serializer.h"
#include "run_command.h"
#include "test_utils.h"

namespace quipper {

// Runs "perf record" to see if the command is available on the current system.
// This should also cover the availability of "perf stat", which is a simpler
// way to get information from the counters.
bool IsPerfRecordAvailable() {
  return RunCommand({"perf", "record", "-a", "-o", "-", "--", "sleep", "0.01"},
                    NULL) == 0;
}

// Runs "perf mem record" to see if the command is available on the current
// system.
bool IsPerfMemRecordAvailable() {
  return RunCommand({"perf", "mem", "record", "-a", "-e", "cycles", "--",
                     "sleep", "0.01"},
                    NULL) == 0;
}

class PerfRecorderTest : public ::testing::Test {
 public:
  PerfRecorderTest() : perf_recorder_({"sudo", GetPerfPath()}) {}

 protected:
  PerfRecorder perf_recorder_;
};

TEST_F(PerfRecorderTest, RecordToProtobuf) {
  // Read perf data using the PerfReader class.
  // Dump it to a string and convert to a protobuf.
  // Read the protobuf, and reconstruct the perf data.
  string output_string;
  EXPECT_TRUE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "record"}, 0.2, &output_string));

  quipper::PerfDataProto perf_data_proto;
  EXPECT_TRUE(perf_data_proto.ParseFromString(output_string));

  const auto& string_meta = perf_data_proto.string_metadata();
  const auto& command = string_meta.perf_command_line_token();
  EXPECT_EQ(GetPerfPath(), command.Get(0).value());
  EXPECT_EQ("record", command.Get(1).value());
  EXPECT_EQ("-o", command.Get(2).value());

  // Unpredictable: EXPECT_EQ("/tmp/quipper.XXXXXX", command.Get(3).value());
  // Instead, check the file path length and prefix.
  EXPECT_EQ(strlen("/tmp/quipper.XXXXXX"), command.Get(3).value().size());
  EXPECT_EQ("/tmp/quipper",
            command.Get(3).value().substr(0, strlen("/tmp/quipper")));

  EXPECT_EQ("--", command.Get(4).value());
  EXPECT_EQ("sleep", command.Get(5).value());
  EXPECT_EQ("0.2", command.Get(6).value());
}

TEST_F(PerfRecorderTest, StatToProtobuf) {
  // Run perf stat and verify output.
  string output_string;
  EXPECT_TRUE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "stat"}, 0.2, &output_string));

  EXPECT_GT(output_string.size(), 0);
  quipper::PerfStatProto stat;
  ASSERT_TRUE(stat.ParseFromString(output_string));
  EXPECT_GT(stat.line_size(), 0);
}

TEST_F(PerfRecorderTest, MemRecordToProtobuf) {
  if (!IsPerfMemRecordAvailable()) return;

  // Run perf mem record and verify output.
  string output_string;
  EXPECT_TRUE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "mem", "record"}, 0.2, &output_string));

  EXPECT_GT(output_string.size(), 0);
  quipper::PerfDataProto perf_data_proto;
  ASSERT_TRUE(perf_data_proto.ParseFromString(output_string));
}

TEST_F(PerfRecorderTest, StatSingleEvent) {
  string output_string;
  ASSERT_TRUE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "stat", "-a", "-e", "cycles"}, 0.2, &output_string));

  EXPECT_GT(output_string.size(), 0);

  quipper::PerfStatProto stat;
  ASSERT_TRUE(stat.ParseFromString(output_string));
  // Replace the placeholder "perf" with the actual perf path.
  string expected_command_line =
      string("sudo ") + GetPerfPath() + " stat -a -e cycles -v -- sleep 0.2";
  EXPECT_EQ(expected_command_line, stat.command_line());

  // Make sure the event counter was read.
  ASSERT_EQ(1, stat.line_size());
  EXPECT_TRUE(stat.line(0).has_time_ms());
  EXPECT_TRUE(stat.line(0).has_count());
  EXPECT_TRUE(stat.line(0).has_event_name());
  // Running for at least one second.
  EXPECT_GE(stat.line(0).time_ms(), 200);
  EXPECT_EQ("cycles", stat.line(0).event_name());
}

TEST_F(PerfRecorderTest, StatMultipleEvents) {
  string output_string;
  ASSERT_TRUE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "stat", "-a", "-e", "cycles", "-e", "instructions", "-e",
       "branches", "-e", "branch-misses"},
      0.2, &output_string));

  EXPECT_GT(output_string.size(), 0);

  quipper::PerfStatProto stat;
  ASSERT_TRUE(stat.ParseFromString(output_string));
  // Replace the placeholder "perf" with the actual perf path.
  string command_line = string("sudo ") + GetPerfPath() +
                        " stat -a "
                        "-e cycles "
                        "-e instructions "
                        "-e branches "
                        "-e branch-misses "
                        "-v "
                        "-- sleep 0.2";
  EXPECT_TRUE(stat.has_command_line());
  EXPECT_EQ(command_line, stat.command_line());

  // Make sure all event counters were read.
  // Check:
  // - Number of events.
  // - Running for at least two seconds.
  // - Event names recorded properly.
  ASSERT_EQ(4, stat.line_size());

  EXPECT_TRUE(stat.line(0).has_time_ms());
  EXPECT_TRUE(stat.line(0).has_count());
  EXPECT_TRUE(stat.line(0).has_event_name());
  EXPECT_GE(stat.line(0).time_ms(), 200);
  EXPECT_EQ("cycles", stat.line(0).event_name());

  EXPECT_TRUE(stat.line(1).has_time_ms());
  EXPECT_TRUE(stat.line(1).has_count());
  EXPECT_TRUE(stat.line(1).has_event_name());
  EXPECT_GE(stat.line(1).time_ms(), 200);
  EXPECT_EQ("instructions", stat.line(1).event_name());

  EXPECT_TRUE(stat.line(2).has_time_ms());
  EXPECT_TRUE(stat.line(2).has_count());
  EXPECT_TRUE(stat.line(2).has_event_name());
  EXPECT_GE(stat.line(2).time_ms(), 200);
  EXPECT_EQ("branches", stat.line(2).event_name());

  EXPECT_TRUE(stat.line(3).has_time_ms());
  EXPECT_TRUE(stat.line(3).has_count());
  EXPECT_TRUE(stat.line(3).has_event_name());
  EXPECT_GE(stat.line(3).time_ms(), 200);
  EXPECT_EQ("branch-misses", stat.line(3).event_name());
}

TEST_F(PerfRecorderTest, DontAllowCommands) {
  string output_string;
  EXPECT_FALSE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "record", "--", "sh", "-c", "echo 'malicious'"}, 0.2,
      &output_string));
  EXPECT_FALSE(perf_recorder_.RunCommandAndGetSerializedOutput(
      {"perf", "stat", "--", "sh", "-c", "echo 'malicious'"}, 0.2,
      &output_string));
}

TEST(PerfRecorderNoPerfTest, FailsIfPerfDoesntExist) {
  string output_string;
  PerfRecorder perf_recorder({"sudo", "/doesnt-exist/usr/not-bin/not-perf"});
  EXPECT_FALSE(perf_recorder.RunCommandAndGetSerializedOutput(
      {"perf", "record"}, 0.2, &output_string));
}

}  // namespace quipper

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  if (!quipper::IsPerfRecordAvailable()) return 0;
  return RUN_ALL_TESTS();
}
