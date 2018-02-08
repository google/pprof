// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_stat_parser.h"

#include <stdlib.h>  // for strtoull and strtod

#include <vector>

#include "base/logging.h"

#include "compat/proto.h"
#include "file_utils.h"
#include "string_utils.h"

namespace quipper {

bool ParsePerfStatFileToProto(const string& path, PerfStatProto* proto) {
  std::vector<char> data;
  if (!FileToBuffer(path, &data)) {
    return false;
  }
  string data_str(data.begin(), data.end());
  return ParsePerfStatOutputToProto(data_str, proto);
}

bool ParsePerfStatOutputToProto(const string& data, PerfStatProto* proto) {
  std::vector<string> lines;
  SplitString(data, '\n', &lines);
  uint64_t time_ms = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    TrimWhitespace(&lines[i]);
    // Look for lines of the form:
    // "name: 123 123 123"
    // OR
    // "1.234 seconds time elapsed"
    std::vector<string> tokens;
    SplitString(lines[i], ' ', &tokens);
    if (tokens.size() != 4) {
      continue;
    }
    const string& first_token = tokens[0];
    // Look for "name: 123 123 123"
    if (first_token.back() == ':') {
      char* endptr;
      uint64_t count = strtoull(tokens[1].c_str(), &endptr, 10);
      if (*endptr != '\0') {
        continue;
      }
      auto newline = proto->add_line();
      newline->set_count(count);
      newline->set_event_name(first_token.substr(0, first_token.size() - 1));
    }
    // Look for "1.234 seconds time elapsed"
    if (tokens[1] == "seconds" &&
        !SecondsStringToMillisecondsUint64(first_token, &time_ms)) {
      time_ms = 0;
    }
  }
  if (time_ms != 0) {
    for (int i = 0; i < proto->line_size(); ++i) {
      proto->mutable_line(i)->set_time_ms(time_ms);
    }
  }
  return proto->line_size() > 0;
}

bool SecondsStringToMillisecondsUint64(const string& str, uint64_t* out) {
  char* endptr;
  double seconds = strtod(str.c_str(), &endptr);
  if (*endptr != '\0') {
    return false;
  }
  if (seconds < 0) {
    return false;
  }
  *out = (static_cast<uint64_t>(seconds * 1000.0 + 0.5));
  return true;
}

}  // namespace quipper
