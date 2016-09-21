// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "chromiumos-wide-profiling/compat/string.h"
#include "chromiumos-wide-profiling/perf_parser.h"

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

// Given a perf data file, get the list of build ids and create a map from
// filenames to build ids.
bool GetPerfBuildIDMap(const string& filename,
                       std::map<string, string>* output);

bool CheckPerfDataAgainstBaseline(const string& filename);

// Returns true if the perf buildid-lists are the same.
bool ComparePerfBuildIDLists(const string& file1, const string& file2);

// Returns options suitable for correctness tests.
PerfParserOptions GetTestOptions();

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_
