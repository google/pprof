/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PERF_DATA_CONVERTER_H_
#define PERFTOOLS_PERF_DATA_CONVERTER_H_

#include <memory>
#include <vector>

#include "int_compat.h"
#include "string_compat.h"
#include "profile.pb.h"

namespace quipper {
class PerfDataProto;
}  // namespace quipper

namespace perftools {


// Sample label options.
enum SampleLabels {
  kNoLabels = 0,
  // Adds label with key PidLabelKey and number value set to the process ID.
  kPidLabel = 1,
  // Adds label with key TidLabelKey and number value set to the thread ID.
  kTidLabel = 2,
  // Equivalent to kPidLabel | kTidLabel
  kPidAndTidLabels = 3,
  // Adds label with key TimestampNsLabelKey and number value set to the number
  // of nanoseconds since the system boot that this sample was taken.
  kTimestampNsLabel = 4,
  // Adds label with key ExecutionModeLabelKey and string value set to one of
  // the ExecutionMode* values.
  kExecutionModeLabel = 8,
  // Adds a label with key CommLabelKey and string value set to the sample's
  // process's command. If no command is known, no label is added.
  kCommLabel = 16,
};

// Sample label key names.
const char PidLabelKey[] = "pid";
const char TidLabelKey[] = "tid";
const char TimestampNsLabelKey[] = "timestamp_ns";
const char ExecutionModeLabelKey[] = "execution_mode";
const char CommLabelKey[] = "comm";

// Execution mode label values.
const char ExecutionModeHostKernel[] = "Host Kernel";
const char ExecutionModeHostUser[] = "Host User";
const char ExecutionModeGuestKernel[] = "Guest Kernel";
const char ExecutionModeGuestUser[] = "Guest User";
const char ExecutionModeHypervisor[] = "Hypervisor";

// Perf data conversion options.
enum ConversionOptions {
  // Default options.
  kNoOptions = 0,
  // Whether to produce multiple, per-process profiles from the single input
  // perf data file. If not set, a single profile will be produced ((but you do
  // still get a list of profiles back; it just has only one entry).
  kGroupByPids = 1,
  // Whether the conversion should fail if there is a detected mismatch between
  // the main mapping in the sample data vs. mapping data.
  kFailOnMainMappingMismatch = 2,
};


struct ProcessProfile {
  // Process PID or 0 if no process grouping was requested.
  // PIDs can duplicate if there was a PID reuse during the profiling session.
  uint32 pid = 0;
  // Profile proto data.
  perftools::profiles::Profile data;
  // Min timestamp of a sample, in nanoseconds since boot, or 0 if unknown.
  int64 min_sample_time_ns = 0;
  // Max timestamp of a sample, in nanoseconds since boot, or 0 if unknown.
  int64 max_sample_time_ns = 0;
};

// Type alias for a random access sequence of owned ProcessProfile objects.
using ProcessProfiles = std::vector<std::unique_ptr<ProcessProfile>>;

// Converts raw Linux perf data to a vector of process profiles.
//
// sample_labels is the OR-product of all SampleLabels desired in the output
// profiles. options governs other conversion options such as whether per-PID
// profiles should be returned or all processes should be merged into the same
// profile.
//
// Returns a vector of process profiles, empty if any error occurs.
extern ProcessProfiles RawPerfDataToProfiles(
    const void* raw, int raw_size, const std::map<string, string>& build_ids,
    uint32 sample_labels = kNoLabels, uint32 options = kGroupByPids);

// Converts a PerfDataProto to a vector of process profiles.
extern ProcessProfiles PerfDataProtoToProfiles(
    const quipper::PerfDataProto* perf_data, uint32 sample_labels = kNoLabels,
    uint32 options = kGroupByPids);

}  // namespace perftools

#endif  // PERFTOOLS_PERF_DATA_CONVERTER_H_
