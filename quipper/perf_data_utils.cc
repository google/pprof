// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_data_utils.h"

#include <string>

#include "base/logging.h"
#include "compat/proto.h"

namespace quipper {

event_t* CallocMemoryForEvent(size_t size) {
  event_t* event = reinterpret_cast<event_t*>(calloc(1, size));
  CHECK(event);
  return event;
}

event_t* ReallocMemoryForEvent(event_t* event, size_t new_size) {
  event_t* new_event = reinterpret_cast<event_t*>(realloc(event, new_size));
  CHECK(new_event);  // NB: event is "leaked" if this CHECK fails.
  return new_event;
}

build_id_event* CallocMemoryForBuildID(size_t size) {
  build_id_event* event = reinterpret_cast<build_id_event*>(calloc(1, size));
  CHECK(event);
  return event;
}

void PerfizeBuildIDString(string* build_id) {
  build_id->resize(kBuildIDStringLength, '0');
}

void TrimZeroesFromBuildIDString(string* build_id) {
  const size_t kPaddingSize = 8;
  const string kBuildIDPadding = string(kPaddingSize, '0');

  // Remove kBuildIDPadding from the end of build_id until we cannot remove any
  // more. The build ID string can be reduced down to an empty string. This
  // could happen if the file did not have a build ID but was given a build ID
  // of all zeroes. The empty build ID string would reflect the original lack of
  // build ID.
  while (build_id->size() >= kPaddingSize &&
         build_id->substr(build_id->size() - kPaddingSize) == kBuildIDPadding) {
    build_id->resize(build_id->size() - kPaddingSize);
  }
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
    case PERF_RECORD_AUX:
      return &event.aux_event().sample_info();
  }
  return nullptr;
}

// Returns the correct |sample_time_ns| field of a PerfEvent.
uint64_t GetTimeFromPerfEvent(const PerfDataProto_PerfEvent& event) {
  if (event.header().type() == PERF_RECORD_SAMPLE)
    return event.sample_event().sample_time_ns();

  const auto* sample_info = GetSampleInfoForEvent(event);
  if (sample_info) return sample_info->sample_time_ns();

  return 0;
}

}  // namespace quipper
