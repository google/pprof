// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_reader.h"

#include <string.h>

namespace quipper {

bool BufferReader::ReadData(const size_t size, void* dest) {
  if (offset_ + size > size_) return false;

  memcpy(dest, buffer_ + offset_, size);
  offset_ += size;
  return true;
}

bool BufferReader::ReadString(size_t size, string* str) {
  if (offset_ + size > size_) return false;

  size_t actual_length = strnlen(buffer_ + offset_, size);
  *str = string(buffer_ + offset_, actual_length);
  offset_ += size;
  return true;
}

}  // namespace quipper
