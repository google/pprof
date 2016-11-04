/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Google Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PERFTOOLS_PERF_DATA_CONVERTER_H_
#define PERFTOOLS_PERF_DATA_CONVERTER_H_

#include <memory>
#include <vector>

#include "int_compat.h"
#include "profile.pb.h"
#include "string_compat.h"

namespace quipper {
class PerfDataProto;
}  // namespace quipper

namespace perftools {

enum SampleLabels {
  kNoLabels = 0,
  // Adds label with key PidLabelName and num set to the process id.
  kPidLabel = 1,
  // Adds label with key TidLabelName and num set to the thread id.
  kTidLabel = 2,
  // Equivalent to kPidLabel | kTidLabel
  kPidAndTidLabels = 3,
  // Adds label with key TimestampNsLabelName and num set to the number of
  // nanoseconds since bootup that this sample was taken.
  kTimestampNsLabel = 4,
  // If set, adds a label named execution_mode where the str value equals one
  // of the ExecutionMode string constants.
  kExecutionModeLabel = 8
};

const char PidLabelKey[] = "pid";
const char TidLabelKey[] = "tid";
const char TimestampNsLabelKey[] = "timestamp_ns";
const char ExecutionModeLabelKey[] = "execution_mode";

const char ExecutionModeHostKernel[] = "Host Kernel";
const char ExecutionModeHostUser[] = "Host User";
const char ExecutionModeGuestKernel[] = "Guest Kernel";
const char ExecutionModeGuestUser[] = "Guest User";
const char ExecutionModeHypervisor[] = "Hypervisor";

// Converts a perf.data and a serialized StringMap to an indexed
// sequence of Profile objects.
//
// sample_labels is the OR-product of all SampleLabels desired in the output
// Profile protos. group_by_pids governs whether multiple PIDs are each grouped
// into their own Profile proto, or if a common Profile proto holds all PIDs.
//
// Returns a list of Profile objects, one per PID with observed samples in the
// profile.  If group_by_pids is set to false, the inner vector will have only
// one Profile proto, into which samples from all pids are bundled.
//
// Returns an empty vector if any error occurs.
extern std::vector<std::unique_ptr<perftools::profiles::Profile> >
RawPerfDataToProfileProto(const void* raw, int raw_size,
                          const std::map<string, string> &build_id_map,
                          uint32 sample_labels = kNoLabels,
                          bool group_by_pids = true);

extern std::vector<std::unique_ptr<perftools::profiles::Profile> >
SerializedPerfDataProtoToProfileProto(const string& serialized_perf_data,
                                      uint32 sample_labels = kNoLabels,
                                      bool group_by_pids = true);

// Returns a serialized StringList of unique mmapped filenames in a
// raw perf.data.
string RawPerfDataUniqueMappedFiles(const void* raw, int raw_size);

}  // namespace perftools

#endif  // PERFTOOLS_PERF_DATA_CONVERTER_H_
