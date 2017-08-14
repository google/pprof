// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_PROTOBUF_IO_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_PROTOBUF_IO_H_

#include "compat/proto.h"
#include "compat/string.h"
#include "perf_parser.h"
#include "perf_reader.h"

namespace quipper {

// Convert a raw perf data file to a PerfDataProto protobuf. Uses PerfParser to
// to process the data before writing it to the protobuf.
bool SerializeFromFile(const string& filename, PerfDataProto* proto);

// Same as SerializeFromFile(), but passes the given PerfParserOptions to
// PerfParser.
bool SerializeFromFileWithOptions(const string& filename,
                                  const PerfParserOptions& options,
                                  PerfDataProto* proto);

// Convert a PerfDataProto to raw perf data, storing it in a file.
bool DeserializeToFile(const PerfDataProto& proto, const string& filename);

// Writes PerfDataProto object to a file as serialized protobuf data.
bool WriteProtobufToFile(const quipper::PerfDataProto& perf_data_proto,
                         const string& filename);

// Read from a file containing serialized PerfDataProto data into a
// PerfDataProto object.
bool ReadProtobufFromFile(quipper::PerfDataProto* perf_data_proto,
                          const string& filename);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_PROTOBUF_IO_H_
