// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_reader.h"

#include <algorithm>

#include "base/logging.h"

#include "binary_data_utils.h"

namespace quipper {

bool DataReader::ReadDataString(const size_t size, string* dest) {
  if (size == 0) {
    dest->clear();
    return true;
  }
  const size_t orig_size = dest->size();
  dest->resize(std::max(size, orig_size));
  bool ret = ReadData(size, &(*dest)[0]);
  dest->resize(ret ? size : orig_size);
  return ret;
}

bool DataReader::ReadDataValue(const size_t size, const string& value_name,
                               void* dest) {
  if (ReadData(size, dest)) return true;
  LOG(ERROR) << "Unable to read " << value_name << ". Requested " << size
             << " bytes, " << size_ - Tell() << " bytes remaining.";
  return false;
}

bool DataReader::ReadStringWithSizeFromData(string* dest) {
  uint32_t len = 0;
  if (!ReadUint32(&len)) {
    LOG(ERROR) << "Could not read string length from data.";
    return false;
  }

  if (!ReadString(len, dest)) {
    LOG(ERROR) << "Failed to read string from data. len: " << len;
    return false;
  }
  return true;
}

}  // namespace quipper
