// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "compat/string.h"
#include "compat/test.h"
#include "file_utils.h"
#include "perf_parser.h"

namespace quipper {

extern const char* kSupportedMetadata[];

// Container for all the metadata from one perf report.  The key is the metadata
// type, as shown in |kSupportedMetadata|.  The value is a vector of all the
// occurrences of that type.  For some types, there is only one occurrence.
typedef std::map<string, std::vector<string> > MetadataSet;

// Path to the perf executable.
string GetPerfPath();

// Converts a perf data filename to the full path.
string GetTestInputFilePath(const string& filename);

// Returns the size of a file in bytes.
int64_t GetFileSize(const string& filename);

// Returns true if the contents of the two files are the same, false otherwise.
bool CompareFileContents(const string& filename1, const string& filename2);

template <typename T>
void CompareTextProtoFiles(const string& filename1, const string& filename2) {
  std::vector<char> file1_contents;
  std::vector<char> file2_contents;
  ASSERT_TRUE(FileToBuffer(filename1, &file1_contents));
  ASSERT_TRUE(FileToBuffer(filename2, &file2_contents));

  ArrayInputStream arr1(file1_contents.data(), file1_contents.size());
  ArrayInputStream arr2(file2_contents.data(), file2_contents.size());

  T proto1, proto2;
  ASSERT_TRUE(TextFormat::Parse(&arr1, &proto1));
  ASSERT_TRUE(TextFormat::Parse(&arr2, &proto2));

  EXPECT_TRUE(EqualsProto(proto1, proto2));
}

// Given a perf data file, get the list of build ids and create a map from
// filenames to build ids.
bool GetPerfBuildIDMap(const string& filename,
                       std::map<string, string>* output);

bool CheckPerfDataAgainstBaseline(const string& filename);

// Returns true if the perf buildid-lists are the same.
bool ComparePerfBuildIDLists(const string& file1, const string& file2);

// Returns options suitable for correctness tests.
PerfParserOptions GetTestOptions();

template <typename T>
bool EqualsProto(T actual, T expected) {
  MessageDifferencer differencer;
  differencer.set_message_field_comparison(MessageDifferencer::EQUAL);
  return differencer.Compare(expected, actual);
}

template <typename T>
bool PartiallyEqualsProto(T actual, T expected) {
  MessageDifferencer differencer;
  differencer.set_message_field_comparison(MessageDifferencer::EQUAL);
  differencer.set_scope(MessageDifferencer::PARTIAL);
  return differencer.Compare(expected, actual);
}

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_
