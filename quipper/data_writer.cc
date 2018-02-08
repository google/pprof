// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_writer.h"

#include <stdint.h>

#include "base/logging.h"

#include "perf_data_utils.h"

namespace quipper {

bool DataWriter::WriteDataValue(const void* src, const size_t size,
                                const string& value_name) {
  if (WriteData(src, size)) return true;
  LOG(ERROR) << "Unable to write " << value_name << ". Requested " << size
             << " bytes, " << size_ - Tell() << " bytes remaining.";
  return false;
}

bool DataWriter::WriteStringWithSizeToData(const string& src) {
  uint32_t len = GetUint64AlignedStringLength(src);
  if (!CanWriteSize(len + sizeof(len))) {
    LOG(ERROR) << "Not enough space to write string.";
    return false;
  }

  if (!WriteDataValue(&len, sizeof(len), "string length") ||
      !WriteString(src, len)) {
    LOG(ERROR) << "Failed to write string.";
    return false;
  }
  return true;
}

}  // namespace quipper
