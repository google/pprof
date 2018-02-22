/*
 * Copyright (c) 2018, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PERF_TO_PROFILE_LIB_H_
#define PERFTOOLS_PERF_TO_PROFILE_LIB_H_

#include <unistd.h>
#include <fstream>

#include "base/logging.h"
#include "string_compat.h"

// Checks and returns whether or not the file at the given |path| already
// exists.
bool FileExists(const string& path);

// Reads a file at the given |path| as a string and returns it.
string ReadFileToString(const string& path);

// Creates a file at the given |path|. If |overwriteOutput| is set to true,
// overwrites the file at the given path.
void CreateFile(const string& path, std::ofstream* file, bool overwriteOutput);

// Parses arguments, stores the results in |input|, |output| and
// |overwriteOutput|, and returns true if arguments parsed successfully and
// false otherwise.
bool ParseArguments(int argc, const char* argv[], string* input, string* output,
                    bool* overwriteOutput);

// Prints the usage of the tool.
void PrintUsage();

#endif  // PERFTOOLS_PERF_TO_PROFILE_LIB_H_
