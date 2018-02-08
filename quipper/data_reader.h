// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_DATA_READER_H_
#define CHROMIUMOS_WIDE_PROFILING_DATA_READER_H_

#include <stddef.h>
#include <stdint.h>

#include "binary_data_utils.h"
#include "compat/string.h"

namespace quipper {

class DataReader {
 public:
  DataReader() : is_cross_endian_(false) {}
  virtual ~DataReader() {}

  // Moves the data read pointer to |offset| bytes from the beginning of the
  // data.
  virtual void SeekSet(size_t offset) = 0;

  // Returns the position of the data read pointer, in bytes from the beginning
  // of the data.
  virtual size_t Tell() const = 0;

  virtual size_t size() const { return size_; }

  // Reads raw data into |dest|. Returns true if it managed to read |size|
  // bytes.
  virtual bool ReadData(const size_t size, void* dest) = 0;

  // Reads raw data into a string.
  virtual bool ReadDataString(const size_t size, string* dest);

  // Like ReadData(), but prints an error if it doesn't read all |size| bytes.
  virtual bool ReadDataValue(const size_t size, const string& value_name,
                             void* dest);

  // Read integers with endian swapping.
  bool ReadUint16(uint16_t* value) { return ReadIntValue(value); }
  bool ReadUint32(uint32_t* value) { return ReadIntValue(value); }
  bool ReadUint64(uint64_t* value) { return ReadIntValue(value); }

  // Read a string. Returns true if it managed to read |size| bytes (excluding
  // null terminator). The actual string may be shorter than the number of bytes
  // requested.
  virtual bool ReadString(const size_t size, string* str) = 0;

  // Reads a string from data into |dest| at the current offset. The string in
  // data is prefixed with a 32-bit size field. The size() of |*dest| after the
  // read will be the null-terminated string length of the underlying string
  // data, and not necessarily the same as the size field in the data.
  bool ReadStringWithSizeFromData(string* dest);

  bool is_cross_endian() const { return is_cross_endian_; }

  void set_is_cross_endian(bool value) { is_cross_endian_ = value; }

 protected:
  // Size of the data source.
  size_t size_;

 private:
  // Like ReadData(), but used specifically to read integers. Will swap byte
  // order if necessary.
  // For type-safety this one private and let public member functions call it.
  template <typename T>
  bool ReadIntValue(T* dest) {
    if (!ReadData(sizeof(T), dest)) return false;
    *dest = MaybeSwap(*dest, is_cross_endian_);
    return true;
  }

  // For cross-endian data reading.
  bool is_cross_endian_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_DATA_READER_H_
