// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_FILE_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_FILE_UTILS_H_

#include <vector>

#include "base/logging.h"
#include "compat/string.h"

namespace quipper {

// Reads the contents of a file into |contents|. Returns true on success, false
// if it fails.
bool FileToBuffer(const string& filename, std::vector<char>* contents);

// Writes |contents| to a file, overwriting the file if it exists. Returns true
// on success, false if it fails.
template <typename CharContainer>
bool BufferToFile(const string& filename, const CharContainer& contents) {
  FILE* fp = fopen(filename.c_str(), "wb");
  if (!fp) return false;
  // Do not write anything if |contents| contains nothing.  fopen will create
  // an empty file.
  if (!contents.empty()) {
    CHECK_EQ(fwrite(contents.data(), sizeof(typename CharContainer::value_type),
                    contents.size(), fp),
             contents.size());
  }
  fclose(fp);
  return true;
}

// Returns true iff the file exists.
bool FileExists(const string& filename);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_FILE_UTILS_H_
