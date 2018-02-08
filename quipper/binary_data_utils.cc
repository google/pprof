// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binary_data_utils.h"

#include <openssl/md5.h>
#include <sys/stat.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>  
#include <iomanip>

#include "base/logging.h"
#include "base/macros.h"

namespace {

// Number of hex digits in a byte.
const int kNumHexDigitsInByte = 2;

}  // namespace

namespace quipper {

static uint64_t Md5Prefix(const unsigned char* data,
                          unsigned long length) {  
  uint64_t digest_prefix = 0;
  unsigned char digest[MD5_DIGEST_LENGTH + 1];

  MD5(data, length, digest);
  // We need 64-bits / # of bits in a byte.
  std::stringstream ss;
  for (size_t i = 0; i < sizeof(uint64_t); i++)
    // The setw(2) and setfill('0') calls are needed to make sure we output 2
    // hex characters for every 8-bits of the hash.
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<unsigned int>(digest[i]);
  ss >> digest_prefix;
  return digest_prefix;
}

uint64_t Md5Prefix(const string& input) {
  auto data = reinterpret_cast<const unsigned char*>(input.data());
  return Md5Prefix(data, input.size());
}

uint64_t Md5Prefix(const std::vector<char>& input) {
  auto data = reinterpret_cast<const unsigned char*>(input.data());
  return Md5Prefix(data, input.size());
}

string RawDataToHexString(const u8* array, size_t length) {
  // Convert the bytes to hex digits one at a time.
  // There will be kNumHexDigitsInByte hex digits, and 1 char for NUL.
  char buffer[kNumHexDigitsInByte + 1];
  string result = "";
  for (size_t i = 0; i < length; ++i) {
    snprintf(buffer, sizeof(buffer), "%02x", array[i]);
    result += buffer;
  }
  return result;
}

string RawDataToHexString(const string& str) {
  return RawDataToHexString(reinterpret_cast<const u8*>(str.data()),
                            str.size());
}

bool HexStringToRawData(const string& str, u8* array, size_t length) {
  const int kHexRadix = 16;
  char* err;
  // Loop through kNumHexDigitsInByte characters at a time (to get one byte)
  // Stop when there are no more characters, or the array has been filled.
  for (size_t i = 0; (i + 1) * kNumHexDigitsInByte <= str.size() && i < length;
       ++i) {
    string one_byte = str.substr(i * kNumHexDigitsInByte, kNumHexDigitsInByte);
    array[i] = strtol(one_byte.c_str(), &err, kHexRadix);
    if (*err) return false;
  }
  return true;
}

}  // namespace quipper
