/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Google Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
    std::cerr << "Usage: convert_perf_data <input perf data> <output profile>"
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
