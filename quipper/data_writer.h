// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_DATA_WRITER_H_
#define CHROMIUMOS_WIDE_PROFILING_DATA_WRITER_H_

#include <stddef.h>

#include "compat/string.h"

namespace quipper {

// Interface for writing data to a destination. The nature of the destination is
// unspecified, and is to be specified by derived classes.
class DataWriter {
 public:
  virtual ~DataWriter() {}

  // Moves the data pointer to |offset| bytes from the beginning of the data.
  virtual void SeekSet(size_t offset) = 0;

  // Returns the position of the data pointer, in bytes from the beginning of
  // the data.
  virtual size_t Tell() const = 0;

  virtual size_t size() const { return size_; }

  // Writes raw data. Returns true if it managed to write |size| bytes.
  virtual bool WriteData(const void* src, const size_t size) = 0;

  // Like WriteData(), but prints an error if it doesn't write all |size| bytes.
  virtual bool WriteDataValue(const void* src, const size_t size,
                              const string& value_name);

  // Writes a string. If the string length is smaller than |size|, it will fill
  // in the remainder of of the destination memory with zeroes. If the string is
  // longer than |size|, it will truncate the string, and will not add a null
  // terminator. Returns true iff the expected number of bytes were written.
  virtual bool WriteString(const string& str, const size_t size) = 0;

  // Writes a string |src| to data, prefixed with a 32-bit size field. The size
  // is rounded up to the next multiple of uint64.
  bool WriteStringWithSizeToData(const string& src);

 protected:
  // Returns true if |data_size| bytes of data can be written to the current
  // underlying destination.
  virtual bool CanWriteSize(size_t data_size) = 0;

  // Current data size.
  size_t size_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_DATA_WRITER_H_
