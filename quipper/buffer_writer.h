// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_BUFFER_WRITER_H_
#define CHROMIUMOS_WIDE_PROFILING_BUFFER_WRITER_H_

#include "data_writer.h"

namespace quipper {

// Writes data to a fixed-size memory buffer.
class BufferWriter : public DataWriter {
 public:
  // The destination buffer is indicated by |buffer| and is |size| bytes long.
  BufferWriter(void* buffer, size_t size)
      : buffer_(reinterpret_cast<char*>(buffer)), offset_(0) {
    size_ = size;
  }

  void SeekSet(size_t offset) override { offset_ = offset; }

  size_t Tell() const override { return offset_; }

  bool WriteData(const void* src, const size_t size) override;

  bool WriteString(const string& str, const size_t size) override;

 private:
  bool CanWriteSize(size_t data_size) override;

  // Pointer to the data buffer. Does not own the buffer.
  char* buffer_;

  // Current write offset, in bytes from start of buffer.
  size_t offset_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_BUFFER_WRITER_H_
