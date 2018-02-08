// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_utils.h"

#include <sys/stat.h>

#include "base/logging.h"
#include "file_reader.h"

namespace quipper {

bool FileToBuffer(const string& filename, std::vector<char>* contents) {
  FileReader reader(filename);
  if (!reader.IsOpen()) return false;
  size_t file_size = reader.size();
  contents->resize(file_size);
  // Do not read anything if the file exists but is empty.
  if (file_size > 0 && !reader.ReadData(file_size, contents->data())) {
    LOG(ERROR) << "Failed to read " << file_size << " bytes from file "
               << filename << ", only read " << reader.Tell();
    return false;
  }
  return true;
}

bool FileExists(const string& filename) {
  struct stat st;
  return stat(filename.c_str(), &st) == 0;
}

}  // namespace quipper
