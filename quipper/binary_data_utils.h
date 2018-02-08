// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_BINARY_DATA_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_BINARY_DATA_UTILS_H_

#include <byteswap.h>
#include <limits.h>
#include <stdint.h>

#include <bitset>
#include <type_traits>
#include <vector>

#include "base/logging.h"

#include "compat/string.h"
#include "kernel/perf_internals.h"

namespace quipper {

// Swaps the byte order of 16-bit, 32-bit, and 64-bit unsigned integers.
template <class T>
void ByteSwap(T* input) {
  switch (sizeof(T)) {
    case sizeof(uint8_t):
      LOG(WARNING) << "Attempting to byte swap on a single byte.";
      break;
    case sizeof(uint16_t):
      *input = bswap_16(*input);
      break;
    case sizeof(uint32_t):
      *input = bswap_32(*input);
      break;
    case sizeof(uint64_t):
      *input = bswap_64(*input);
      break;
    default:
      LOG(FATAL) << "Invalid size for byte swap: " << sizeof(T) << " bytes";
      break;
  }
}

// Swaps byte order of |value| if the |swap| flag is set. This function is
// trivial but it avoids filling code with "if (swap) { ... } " statements.
template <typename T>
T MaybeSwap(T value, bool swap) {
  if (swap) ByteSwap(&value);
  return value;
}

// Returns the number of bits in a numerical value.
template <typename T>
size_t GetNumBits(const T& value) {
  return std::bitset<sizeof(T) * CHAR_BIT>(value).count();
}

// Returns the leading 64 bits of the MD5 digest of |input|.
uint64_t Md5Prefix(const string& input);
uint64_t Md5Prefix(const std::vector<char>& input);

// Returns a string that represents |array| in hexadecimal.
string RawDataToHexString(const u8* array, size_t length);

// Given raw data in |str|, returns a string that represents the binary data as
// hexadecimal.
string RawDataToHexString(const string& str);

// Given a string |str| containing data represented in hexadecimal, converts to
// to raw bytes stored in |array|.  Returns true on success.  Only stores up to
// |length| bytes - if there are more characters in the string, they are
// ignored (but the function may still return true).
bool HexStringToRawData(const string& str, u8* array, size_t length);

// Round |value| up to the next |alignment|. I.e. returns the smallest multiple
// of |alignment| less than or equal to |value|. |alignment| must be a power
// of 2 (compile-time enforced).
// clang-format off
template<unsigned int alignment,
         typename std::enable_if<
             alignment != 0 && (alignment&(alignment-1)) == 0
         >::type* = nullptr>
// clang-format on
inline uint64_t Align(uint64_t value) {
  constexpr uint64_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

// Allows passing a type parameter instead of a size.
template <typename T>
inline uint64_t Align(uint64_t value) {
  return Align<sizeof(T)>(value);
}

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_BINARY_DATA_UTILS_H_
