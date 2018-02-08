// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_protobuf_io.h"

#include <vector>

#include "base/logging.h"

#include "file_utils.h"

namespace quipper {

bool SerializeFromFile(const string& filename, PerfDataProto* perf_data_proto) {
  return SerializeFromFileWithOptions(filename, PerfParserOptions(),
                                      perf_data_proto);
}

bool SerializeFromFileWithOptions(const string& filename,
                                  const PerfParserOptions& options,
                                  PerfDataProto* perf_data_proto) {
  PerfReader reader;
  if (!reader.ReadFile(filename)) return false;

  PerfParser parser(&reader, options);
  if (!parser.ParseRawEvents()) return false;

  if (!reader.Serialize(perf_data_proto)) return false;

  // Append parser stats to protobuf.
  PerfSerializer::SerializeParserStats(parser.stats(), perf_data_proto);
  return true;
}

bool DeserializeToFile(const PerfDataProto& perf_data_proto,
                       const string& filename) {
  PerfReader reader;
  return reader.Deserialize(perf_data_proto) && reader.WriteFile(filename);
}

bool WriteProtobufToFile(const PerfDataProto& perf_data_proto,
                         const string& filename) {
  string output;
  perf_data_proto.SerializeToString(&output);

  return BufferToFile(filename, output);
}

bool ReadProtobufFromFile(PerfDataProto* perf_data_proto,
                          const string& filename) {
  std::vector<char> buffer;
  if (!FileToBuffer(filename, &buffer)) return false;

  bool ret = perf_data_proto->ParseFromArray(buffer.data(), buffer.size());

  LOG(INFO) << "#events" << perf_data_proto->events_size();

  return ret;
}

}  // namespace quipper
