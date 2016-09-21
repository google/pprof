// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/utils.h"

#include <openssl/md5.h>
#include <sys/stat.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>  // NOLINT(readability/streams)
#include <iomanip>
#include <sstream>

#include "base/logging.h"
#include "base/macros.h"
#include "chromiumos-wide-profiling/compat/proto.h"

namespace {

// Number of hex digits in a byte.
const int kNumHexDigitsInByte = 2;

}  // namespace

namespace quipper {

int64_t GetFileSizeFromHandle(FILE* fp) {
  int64_t position = ftell(fp);
  fseek(fp, 0, SEEK_END);
  int64_t file_size = ftell(fp);
  // Restore the original file handle position.
  fseek(fp, position, SEEK_SET);
  return file_size;
}

static uint64_t Md5Prefix(
    const unsigned char* data,
    unsigned long length) { // NOLINT
  uint64_t digest_prefix = 0;
  unsigned char digest[MD5_DIGEST_LENGTH + 1];

  MD5(data, length, digest);
  // We need 64-bits / # of bits in a byte.
  stringstream ss;
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

bool FileToBuffer(const string& filename, std::vector<char>* contents) {
  FILE* fp = fopen(filename.c_str(), "rb");
  if (!fp)
    return false;
  int64_t file_size = quipper::GetFileSizeFromHandle(fp);
  contents->resize(file_size);
  // Do not read anything if the file exists but is empty.
  if (file_size > 0)
    CHECK_GT(fread(contents->data(), file_size, 1, fp), 0U);
  fclose(fp);
  return true;
}

bool FileExists(const string& filename) {
  struct stat st;
  return stat(filename.c_str(), &st) == 0;
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
  return
      RawDataToHexString(reinterpret_cast<const u8*>(str.data()), str.size());
}

bool HexStringToRawData(const string& str, u8* array, size_t length) {
  const int kHexRadix = 16;
  char* err;
  // Loop through kNumHexDigitsInByte characters at a time (to get one byte)
  // Stop when there are no more characters, or the array has been filled.
  for (size_t i = 0;
       (i + 1) * kNumHexDigitsInByte <= str.size() && i < length;
       ++i) {
    string one_byte = str.substr(i * kNumHexDigitsInByte, kNumHexDigitsInByte);
    array[i] = strtol(one_byte.c_str(), &err, kHexRadix);
    if (*err)
      return false;
  }
  return true;
}

bool ReadFileToData(const string& filename, std::vector<char>* data) {
  std::ifstream in(filename.c_str(), std::ios::binary);
  if (!in.good()) {
    LOG(ERROR) << "Failed to open file " << filename;
    return false;
  }
  in.seekg(0, in.end);
  size_t length = in.tellg();
  in.seekg(0, in.beg);
  data->resize(length);

  in.read(&(*data)[0], length);

  if (!in.good()) {
    LOG(ERROR) << "Error reading from file " << filename;
    return false;
  }
  return true;
}

bool WriteDataToFile(const std::vector<char>& data, const string& filename) {
  std::ofstream out(filename.c_str(), std::ios::binary);
  out.seekp(0, std::ios::beg);
  out.write(&data[0], data.size());
  return out.good();
}

void TrimWhitespace(string* str) {
  const char kWhitespaceCharacters[] = " \t\n\r";
  size_t end = str->find_last_not_of(kWhitespaceCharacters);
  if (end != std::string::npos) {
    size_t start = str->find_first_not_of(kWhitespaceCharacters);
    *str = str->substr(start, end + 1 - start);
  } else {
    // The string contains only whitespace.
    *str = "";
  }
}

void SplitString(const string& str,
                 char delimiter,
                 std::vector<string>* tokens) {
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delimiter))
    tokens->push_back(token);
}

const PerfDataProto_SampleInfo* GetSampleInfoForEvent(
    const PerfDataProto_PerfEvent& event) {
  switch (event.header().type()) {
  case PERF_RECORD_MMAP:
  case PERF_RECORD_MMAP2:
    return &event.mmap_event().sample_info();
  case PERF_RECORD_COMM:
    return &event.comm_event().sample_info();
  case PERF_RECORD_FORK:
    return &event.fork_event().sample_info();
  case PERF_RECORD_EXIT:
    return &event.exit_event().sample_info();
  case PERF_RECORD_LOST:
    return &event.lost_event().sample_info();
  case PERF_RECORD_THROTTLE:
  case PERF_RECORD_UNTHROTTLE:
    return &event.throttle_event().sample_info();
  case PERF_RECORD_READ:
    return &event.read_event().sample_info();
  }
  return nullptr;
}

// Returns the correct |sample_time_ns| field of a PerfEvent.
uint64_t GetTimeFromPerfEvent(const PerfDataProto_PerfEvent& event) {
  if (event.header().type() == PERF_RECORD_SAMPLE)
    return event.sample_event().sample_time_ns();

  const auto* sample_info = GetSampleInfoForEvent(event);
  if (sample_info)
    return sample_info->sample_time_ns();

  return 0;
}

}  // namespace quipper
