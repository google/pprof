/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// Tests converting split hugepages mapping into a single mapping.

#include "chrome_huge_pages_mapping_deducer.h"

#include <cstdint>
#include <string>

#include "quipper/perf_data.proto.h"
#include "compat/test_compat.h"

namespace perftools {

namespace {

using MMap = quipper::PerfDataProto_MMapEvent;

// Determines whether a MMap protobuf is uninitialized. The MMap protobuf uses
// the lite message format, which has no Empty() function. Instead, serialize to
// a string and compare against the serialized string of an uninitialized
// protobuf.
bool IsUninitialized(const MMap& mmap) {
  return mmap.SerializeAsString() == MMap().SerializeAsString();
}

class TestDeducer : public ChromeHugePagesMappingDeducer {
 public:
  // Wrapper around ChromeHugePagesMappingDeducer::ProcessMmap() that lets the
  // caller pass in the mapping parameters without having to first create a MMap
  // protobuf.
  void ProcessMmap(const std::string& filename, uint64_t start, uint64_t length,
                   uint64_t file_offset, uint32_t pid) {
    MMap mmap;
    mmap.set_filename(filename);
    mmap.set_start(start);
    mmap.set_len(length);
    mmap.set_pgoff(file_offset);
    mmap.set_pid(pid);

    ChromeHugePagesMappingDeducer::ProcessMmap(mmap);
  }
};

}  // namespace

TEST(ChromeHugePagesMappingDeducerTest, InitialState) {
  TestDeducer deducer;
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));
}

TEST(ChromeHugePagesMappingDeducerTest, NonHugePagesMappings) {
  TestDeducer deducer;

  deducer.ProcessMmap("foo", 0x1000, 0x1000, 0, 1);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("bar", 0x2000, 0x10000, 0, 2);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("goo", 0x12000, 0x4000, 0, 3);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("baz", 0x16000, 0xa000, 0, 4);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x20000, 0x8000, 0, 5);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
}

TEST(ChromeHugePagesMappingDeducerTest, SingleHugePagesMapping) {
  TestDeducer deducer;

  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x20000, 0x8000, 0, 123);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("//anon", 0x28000, 0x1e00000, 0, 123);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e28000, 0x10000,
                      0x1e08000, 123);
  EXPECT_TRUE(deducer.CombinedMappingAvailable());

  const auto& combined_mapping = deducer.combined_mapping();
  EXPECT_EQ("/opt/google/chrome/chrome", combined_mapping.filename());
  EXPECT_EQ(0x20000, combined_mapping.start());
  EXPECT_EQ(0x1e18000, combined_mapping.len());
  EXPECT_EQ(0U, combined_mapping.pgoff());
  EXPECT_EQ(123U, combined_mapping.pid());

  // Add another mapping to the end to reset the deducer.
  deducer.ProcessMmap("foo", 0x1e38000, 0x10000, 0, 123);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));
}

TEST(ChromeHugePagesMappingDeducerTest, MultipleHugepageMappings) {
  TestDeducer deducer;

  deducer.ProcessMmap("//anon", 0x200000, 0x400000, 0, 123);
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x600000, 0x800000, 0x400000,
                      123);
  deducer.ProcessMmap("//anon", 0xe00000, 0x1c00000, 0, 123);
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x2a00000, 0x10000,
                      0x2800000, 123);
  ASSERT_TRUE(deducer.CombinedMappingAvailable());

  const auto& combined_mapping = deducer.combined_mapping();
  EXPECT_EQ("/opt/google/chrome/chrome", combined_mapping.filename());
  EXPECT_EQ(0x200000, combined_mapping.start());
  EXPECT_EQ(0x2810000, combined_mapping.len());
  EXPECT_EQ(0U, combined_mapping.pgoff());
  EXPECT_EQ(123U, combined_mapping.pid());
}

TEST(ChromeHugePagesMappingDeducerTest,
     SingleHugePagesMappingWithoutFirstMapping) {
  TestDeducer deducer;

  deducer.ProcessMmap("//anon", 0x28000, 0x1e00000, 0, 123);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e28000, 0x10000,
                      0x1e08000, 123);
  EXPECT_TRUE(deducer.CombinedMappingAvailable());

  const auto& combined_mapping = deducer.combined_mapping();
  EXPECT_EQ("/opt/google/chrome/chrome", combined_mapping.filename());
  EXPECT_EQ(0x28000, combined_mapping.start());
  EXPECT_EQ(0x1e10000, combined_mapping.len());
  EXPECT_EQ(0x8000, combined_mapping.pgoff());
  EXPECT_EQ(123U, combined_mapping.pid());
}

TEST(ChromeHugePagesMappingDeducerTest, IncorrectHugePageSize) {
  TestDeducer deducer;

  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x20000, 0x8000, 0, 456);
  deducer.ProcessMmap("//anon", 0x28000, 0x1e80000, 0, 456);
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e28000, 0x10000,
                      0x1e08000, 456);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));
}

TEST(ChromeHugePagesMappingDeducerTest, IncorrectFileName) {
  TestDeducer deducer;

  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x20000, 0x8000, 0, 456);
  deducer.ProcessMmap("//anonymous", 0x28000, 0x1e00000, 0, 456);
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e28000, 0x10000,
                      0x1e08000, 456);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));

  deducer.ProcessMmap("//anon", 0x28000, 0x1e00000, 0, 456);
  deducer.ProcessMmap("/opt/google/chrome/bogus", 0x1e28000, 0x10000, 0x1e08000,
                      456);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));
}

TEST(ChromeHugePagesMappingDeducerTest, NoncontiguousMappings) {
  TestDeducer deducer;

  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x20000, 0x8000, 0, 456);
  deducer.ProcessMmap("//anon", 0x29000, 0x1e00000, 0, 456);
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e29000, 0x10000,
                      0x1e08000, 456);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));

  deducer.ProcessMmap("//anon", 0x28000, 0x1e00000, 0, 456);
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e29000, 0x10000,
                      0x1e08000, 456);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  EXPECT_TRUE(IsUninitialized(deducer.combined_mapping()));
}

TEST(ChromeHugePagesMappingDeducerTest, MultipleMappings) {
  TestDeducer deducer;

  deducer.ProcessMmap("foo", 0x1000, 0x1000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("bar", 0x2000, 0x10000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());

  // First valid mapping.
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x20000, 0x8000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("//anon", 0x28000, 0x1e00000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x1e28000, 0x10000,
                      0x1e08000, 789);
  EXPECT_TRUE(deducer.CombinedMappingAvailable());
  EXPECT_EQ("/opt/google/chrome/chrome", deducer.combined_mapping().filename());
  EXPECT_EQ(0x20000, deducer.combined_mapping().start());
  EXPECT_EQ(0x1e18000, deducer.combined_mapping().len());
  EXPECT_EQ(0U, deducer.combined_mapping().pgoff());
  EXPECT_EQ(789U, deducer.combined_mapping().pid());

  // Second valid mapping.
  deducer.ProcessMmap("//anon", 0x40028000, 0x1e00000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x41e28000, 0x10000,
                      0x1e08000, 789);
  EXPECT_TRUE(deducer.CombinedMappingAvailable());

  EXPECT_EQ("/opt/google/chrome/chrome", deducer.combined_mapping().filename());
  EXPECT_EQ(0x40028000, deducer.combined_mapping().start());
  EXPECT_EQ(0x1e10000, deducer.combined_mapping().len());
  EXPECT_EQ(0x8000, deducer.combined_mapping().pgoff());
  EXPECT_EQ(789U, deducer.combined_mapping().pid());

  deducer.ProcessMmap("goo", 0x12000, 0x4000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("baz", 0x16000, 0xa000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());

  // Third valid mapping.
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x7f000000, 0x8000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("//anon", 0x7f008000, 0x1e00000, 0, 789);
  EXPECT_FALSE(deducer.CombinedMappingAvailable());
  deducer.ProcessMmap("/opt/google/chrome/chrome", 0x80e08000, 0x10000,
                      0x1e08000, 789);
  EXPECT_TRUE(deducer.CombinedMappingAvailable());
  EXPECT_EQ("/opt/google/chrome/chrome", deducer.combined_mapping().filename());
  EXPECT_EQ(0x7f000000, deducer.combined_mapping().start());
  EXPECT_EQ(0x1e18000, deducer.combined_mapping().len());
  EXPECT_EQ(0U, deducer.combined_mapping().pgoff());
  EXPECT_EQ(789U, deducer.combined_mapping().pid());
}

}  // namespace perftools

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
