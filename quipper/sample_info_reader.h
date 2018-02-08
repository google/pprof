// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_SAMPLE_INFO_READER_H_
#define CHROMIUMOS_WIDE_PROFILING_SAMPLE_INFO_READER_H_

#include <stdint.h>

#include "kernel/perf_event.h"

namespace quipper {

// Forward declarations of structures.
union perf_event;
typedef perf_event event_t;
struct perf_sample;

class SampleInfoReader {
 public:
  SampleInfoReader(struct perf_event_attr event_attr, bool read_cross_endian)
      : event_attr_(event_attr), read_cross_endian_(read_cross_endian) {}

  bool ReadPerfSampleInfo(const event_t& event,
                          struct perf_sample* sample) const;
  bool WritePerfSampleInfo(const perf_sample& sample, event_t* event) const;

  // Given a general perf sample format |sample_type|, return the fields of that
  // format that are present in a sample for an event of type |event_type|.
  //
  // e.g. FORK and EXIT events have the fields {time, pid/tid, cpu, id}.
  // Given a sample type with fields {ip, time, pid/tid, and period}, return
  // the intersection of these two field sets: {time, pid/tid}.
  //
  // All field formats are bitfields, as defined by enum
  // perf_event_sample_format in kernel/perf_event.h.
  static uint64_t GetSampleFieldsForEventType(uint32_t event_type,
                                              uint64_t sample_type);

  // Returns the offset in bytes within a perf event structure at which the raw
  // perf sample data is located.
  static uint64_t GetPerfSampleDataOffset(const event_t& event);

  const perf_event_attr& event_attr() const { return event_attr_; }

 private:
  // Event attribute info, which determines the contents of some perf_sample
  // data.
  struct perf_event_attr event_attr_;

  // Set this flag if values (uint32s and uint64s) should be endian-swapped
  // during reads.
  bool read_cross_endian_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_SAMPLE_INFO_READER_H_
