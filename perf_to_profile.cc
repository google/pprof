/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <sstream>

#include "perf_data_converter.h"
#include "string_compat.h"

bool FileExists(const char* path) {
  struct stat file_stat;
  return stat(path, &file_stat) != -1;
}

string ReadFileToString(const char* path) {
  std::ifstream perf_file(path);
  if (!perf_file.is_open()) {
    std::cerr << "Failed to open file: " << path << std::endl;
    abort();
  }
  std::ostringstream ss;
  ss << perf_file.rdbuf();
  return ss.str();
}

void CreateFile(const char* path, std::ofstream* file) {
  if (FileExists(path)) {
    std::cerr << "File already exists: " << path << std::endl;
    abort();
  }
  file->open(path);
  if (!file->is_open()) {
    std::cerr << "Failed to open file: " << path << std::endl;
    abort();
  }
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input perf data> <output profile>"
              << std::endl;
    return 1;
  }
  const auto perf_data = ReadFileToString(argv[1]);
  const auto raw_perf_data = static_cast<const void*>(perf_data.data());

  const auto profiles = perftools::RawPerfDataToProfiles(
      raw_perf_data, perf_data.length(), {}, perftools::kNoLabels,
      perftools::kNoOptions);
  // With kNoOptions, all of the PID profiles should be merged into a
  // single one.
  if (profiles.size() != 1) {
    std::cerr << "Expected profile vector to have one element." << std::endl;
    abort();
  }
  const auto& profile = profiles[0]->data;
  std::ofstream output;
  CreateFile(argv[2], &output);
  profile.SerializeToOstream(&output);
  return 0;
}
