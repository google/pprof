// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_BUFFER_READER_H_
#define CHROMIUMOS_WIDE_PROFILING_BUFFER_READER_H_

#include "data_reader.h"

namespace quipper {

// Read from a fixed-size data buffer. Does not take ownership of the buffer.
class BufferReader : public DataReader {
 public:
  // The data source is indicated by |buffer| and is |size| bytes long.
  BufferReader(const void* buffer, size_t size)
      : buffer_(reinterpret_cast<const char*>(buffer)), offset_(0) {
    size_ = size;
  }

  void SeekSet(size_t offset) override { offset_ = offset; }

  size_t Tell() const override { return offset_; }

  bool ReadData(const size_t size, void* dest) override;

  // Reads |size| bytes of the buffer as a null-terminated string into |str|.
  // Trailing nulls, if any, are not added to the string, but they are skipped
  // over. If there is no null terminator within these |size| bytes, then the
  // string is automatically terminated after |size| bytes.
  bool ReadString(const size_t size, string* str) override;

 private:
  // The data buffer from which to read.
  const char* buffer_;

  // Data read offset from the start of |buffer_|.
  size_t offset_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_BUFFER_READER_H_
