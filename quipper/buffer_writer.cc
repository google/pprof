// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_writer.h"

#include <string.h>

#include <algorithm>

namespace quipper {

bool BufferWriter::WriteData(const void* src, const size_t size) {
  // Do not write anything if the write needs to be truncated to avoid writing
  // past the end of the file. The extra conditions here are in case of integer
  // overflow.
  if (offset_ + size > size_ || offset_ >= size_ || size > size_) return false;
  memcpy(buffer_ + offset_, src, size);
  offset_ += size;
  return true;
}

bool BufferWriter::WriteString(const string& str, const size_t size) {
  // Make sure there is enough space left in the buffer.
  size_t write_size = std::min(str.size(), size);
  if (offset_ + size > size_ || !WriteData(str.c_str(), write_size))
    return false;

  // Write the padding, if any. Note that the offset has been updated by
  // WriteData.
  memset(buffer_ + offset_, 0, size - write_size);
  offset_ += size - write_size;
  return true;
}

bool BufferWriter::CanWriteSize(size_t data_size) {
  return Tell() + data_size <= size();
}

}  // namespace quipper
