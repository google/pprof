/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_INTERVALMAP_TEST_H_
#define PERFTOOLS_INTERVALMAP_TEST_H_

#include <utility>
#include <vector>

#include "int_compat.h"
#include "intervalmap.h"
#include "string_compat.h"
#include "test_compat.h"

namespace perftools {
namespace {

class Command {
 public:
  virtual ~Command() {}
  virtual void ExecuteOn(IntervalMap<string>* map) const = 0;
};

class SetCommand : public Command {
 public:
  SetCommand(uint64 start, uint64 limit, const char* value)
      : start_(start), limit_(limit), value_(value) {}

  void ExecuteOn(IntervalMap<string>* map) const override {
    map->Set(start_, limit_, value_);
  }

 private:
  const uint64 start_;
  const uint64 limit_;
  const char* value_;
};

class NumIntervalsCommand : public Command {
 public:
  explicit NumIntervalsCommand(uint64 expected) : expected_(expected) {}

  void ExecuteOn(IntervalMap<string>* map) const override {
    ASSERT_EQ(expected_, map->Size());
  }

 private:
  const uint64 expected_;
};

class LookupCommand : public Command {
 public:
  LookupCommand(uint64 from, uint64 to, const char* expected)
      : from_(from), to_(to), expected_(expected) {}

  void ExecuteOn(IntervalMap<string>* map) const override {
    for (uint64 key = from_; key <= to_; ++key) {
      string result;
      ASSERT_TRUE(map->Lookup(key, &result)) << "Did not find value for key: "
                                             << key;
      ASSERT_EQ(expected_, result) << "For key: " << key
                                   << " Found value: " << result
                                   << ". Expected: " << expected_;
    }
  }

 private:
  const uint64 from_;
  const uint64 to_;
  const char* expected_;
};

class FailLookupCommand : public Command {
 public:
  explicit FailLookupCommand(std::vector<uint64> keys)
      : keys_(std::move(keys)) {}

  void ExecuteOn(IntervalMap<string>* map) const override {
    string result;
    for (auto key : keys_) {
      ASSERT_FALSE(map->Lookup(key, &result)) << "Found value for key: " << key;
    }
  }

 private:
  std::vector<uint64> keys_;
};

class FindNextCommand : public Command {
 public:
  FindNextCommand(uint64 key, uint64 expected_start, uint64 expected_limit,
                  const char* expected_value)
      : key_(key),
        expected_start_(expected_start),
        expected_limit_(expected_limit),
        expected_value_(expected_value) {}

  void ExecuteOn(IntervalMap<string>* map) const override {
    uint64 start;
    uint64 limit;
    string value;
    ASSERT_TRUE(map->FindNext(key_, &start, &limit, &value))
        << "Did not find a next interval for key: " << key_;
    bool matches_expected = expected_start_ == start &&
                            expected_limit_ == limit &&
                            expected_value_ == value;
    ASSERT_TRUE(matches_expected)
        << "Found incorrect interval for key: " << key_ << ". "
        << "Expected start: " << expected_start_ << ". "
        << "Expected limit: " << expected_start_ << ". "
        << "Expected value: " << expected_value_ << ". "
        << "Found start: " << start << ". "
        << "Found limit: " << limit << ". "
        << "Found value: " << value << ". ";
  }

 private:
  uint64 key_;
  uint64 expected_start_;
  uint64 expected_limit_;
  const char* expected_value_;
};

class FailFindNextCommand : public Command {
 public:
  explicit FailFindNextCommand(uint64 key) : key_(key) {}

  void ExecuteOn(IntervalMap<string>* map) const override {
    uint64 start;
    uint64 limit;
    string value;
    ASSERT_FALSE(map->FindNext(key_, &start, &limit, &value))
        << "Found interval for: " << key_ << ". "
        << "start: " << start << ". "
        << "limit: " << limit << ". "
        << "value: " << value << ". "
        << "Did not find a next interval for key: " << key_;
  }

 private:
  uint64 key_;
};

std::shared_ptr<Command> Set(uint64 start, uint64 limit, const char* value) {
  return std::make_shared<SetCommand>(start, limit, value);
}

std::shared_ptr<Command> NumIntervals(uint64 size) {
  return std::make_shared<NumIntervalsCommand>(size);
}

// Looks up every key in the interval [from, to] and expects them all to be
// equal to expected.
std::shared_ptr<Command> Lookup(uint64 from, uint64 to, const char* expected) {
  return std::make_shared<LookupCommand>(from, to, expected);
}

std::shared_ptr<Command> FailLookup(std::vector<uint64> keys) {
  return std::make_shared<FailLookupCommand>(keys);
}

std::shared_ptr<Command> FindNext(uint64 key, uint64 start, uint64 limit,
                                  const char* expected) {
  return std::make_shared<FindNextCommand>(key, start, limit, expected);
}

std::shared_ptr<Command> FailFindNext(uint64 key) {
  return std::make_shared<FailFindNextCommand>(key);
}

class IntervalMapTest
    : public ::testing::TestWithParam<std::vector<std::shared_ptr<Command>>> {};

const std::vector<std::shared_ptr<Command>> tests[] = {
    {
        // Simple set/lookup
        Set(0, 10, "Added"), NumIntervals(1), Lookup(0, 9, "Added"),
        FailLookup({10, 11}),
    },
    {
        // Total overwrite same start
        Set(5, 10, "Added"), Set(5, 20, "Overwrite"), NumIntervals(1),
        Lookup(5, 19, "Overwrite"), FailLookup({3, 4, 20, 21}),
    },
    {
        // No overwrite, start of one equals limit of other
        Set(5, 10, "Segment 1"), Set(10, 20, "Segment 2"), NumIntervals(2),
        Lookup(5, 9, "Segment 1"), Lookup(10, 19, "Segment 2"),
        FailLookup({3, 4, 20, 21}),
    },
    {
        // Right side overwrite
        Set(5, 10, "Added"), Set(8, 12, "Overwrite"), NumIntervals(2),
        Lookup(5, 7, "Added"), Lookup(8, 11, "Overwrite"),
        FailLookup({3, 4, 12, 13}),
    },
    {
        // Left side overwrite
        Set(5, 10, "Added"), Set(3, 8, "Overwrite"), NumIntervals(2),
        Lookup(8, 9, "Added"), Lookup(3, 7, "Overwrite"),
        FailLookup({1, 2, 12, 13}),
    },
    {
        // Total overwrite
        Set(5, 10, "Added"), Set(3, 12, "Overwrite"), NumIntervals(1),
        Lookup(3, 11, "Overwrite"), FailLookup({1, 2, 12, 13}),
    },
    {
        // Internal overwrite
        Set(4, 11, "Added"), Set(6, 9, "Overwrite"), NumIntervals(3),
        Lookup(4, 5, "Added"), Lookup(6, 8, "Overwrite"),
        Lookup(9, 10, "Added"), FailLookup({2, 3, 11, 12}),
    },
    {
        // Exact overwrite
        Set(5, 10, "Added"), Set(5, 10, "Overwrite"), NumIntervals(1),
        Lookup(5, 9, "Overwrite"), FailLookup({3, 4, 10, 11}),
    },
    {
        // Same left side overwrite
        Set(5, 10, "Added"), Set(5, 8, "Overwrite"), NumIntervals(2),
        Lookup(5, 7, "Overwrite"), Lookup(8, 9, "Added"),
        FailLookup({3, 4, 10, 11}),
    },
    {
        // Multiple total overwrite
        Set(5, 10, "SEG 1"), Set(8, 12, "SEG 2"), Set(16, 22, "SEG 3"),
        Set(25, 26, "SEG 4"), Set(3, 30, "Overwrite"), NumIntervals(1),
        Lookup(3, 29, "Overwrite"), FailLookup({1, 2, 30, 31}),
    },
    {
        // Multiple total overwrite, left side free
        Set(5, 10, "SEG 1"), Set(8, 12, "SEG 2"), Set(16, 22, "SEG 3"),
        Set(25, 26, "SEG 4"), Set(7, 30, "Overwrite"), NumIntervals(2),
        Lookup(5, 6, "SEG 1"), Lookup(7, 29, "Overwrite"),
        FailLookup({3, 4, 30, 31}),
    },
    {
        // Multiple total overwrite, right side free
        Set(5, 10, "SEG 1"), Set(8, 12, "SEG 2"), Set(16, 22, "SEG 3"),
        Set(25, 32, "SEG 4"), Set(3, 30, "Overwrite"), NumIntervals(2),
        Lookup(3, 29, "Overwrite"), Lookup(30, 31, "SEG 4"),
        FailLookup({1, 2, 32, 33}),
    },
    {
        // Multiple total overwrite, both sides free
        Set(5, 10, "SEG 1"), Set(8, 12, "SEG 2"), Set(16, 22, "SEG 3"),
        Set(25, 32, "SEG 4"), Set(7, 30, "Overwrite"), NumIntervals(3),
        Lookup(5, 6, "SEG 1"), Lookup(7, 29, "Overwrite"),
        Lookup(30, 31, "SEG 4"), FailLookup({3, 4, 32, 33}),
    },
    {
        // Two segments partly overwritten
        Set(5, 10, "SEG 1"), Set(17, 25, "SEG 2"), Set(8, 20, "Overwrite"),
        NumIntervals(3), Lookup(5, 7, "SEG 1"), Lookup(8, 19, "Overwrite"),
        Lookup(20, 24, "SEG 2"), FailLookup({3, 4, 25, 26}),
    },
    {
        // Loop through interval map using FindNext
        Set(5, 10, "SEG 1"), Set(15, 20, "SEG 2"), FindNext(0, 5, 10, "SEG 1"),
        FindNext(10, 15, 20, "SEG 2"), FailFindNext(20),
    },
};

TEST_P(IntervalMapTest, GenericTest) {
  IntervalMap<string> map;
  const auto& commands = GetParam();
  for (const auto& command : commands) {
    command->ExecuteOn(&map);
    // Failed asserts in subroutines aren't actually fatal so we have to return
    // manually.
    if (HasFatalFailure()) return;
  }
}

INSTANTIATE_TEST_CASE_P(AllIntervalMapTests, IntervalMapTest,
                        ::testing::ValuesIn(tests));

}  // namespace
}  // namespace perftools

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#endif  // PERFTOOLS_INTERVALMAP_TEST_H_
