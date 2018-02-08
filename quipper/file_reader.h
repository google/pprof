// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_FILE_READER_H_
#define CHROMIUMOS_WIDE_PROFILING_FILE_READER_H_

#include <stdio.h>

#include "data_reader.h"

namespace quipper {

// Read from an input file. Must be a normal file. Does not support pipe inputs.
class FileReader : public DataReader {
 public:
  explicit FileReader(const string& filename);
  virtual ~FileReader();

  bool IsOpen() const { return infile_; }

  void SeekSet(size_t offset) override { fseek(infile_, offset, SEEK_SET); }

  size_t Tell() const override { return ftell(infile_); }

  bool ReadData(const size_t size, void* dest) override;

  // If there is a failure reading the data from file, |*str| will not be
  // modified.
  bool ReadString(const size_t size, string* str) override;

 private:
  // File input handle.
  FILE* infile_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_FILE_READER_H_
