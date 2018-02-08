// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_DATA_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_DATA_UTILS_H_

#include <stddef.h>  // for size_t
#include <stdlib.h>  // for free()

#include <memory>

#include "binary_data_utils.h"  // for Align<T>
#include "compat/string.h"
#include "kernel/perf_internals.h"

namespace quipper {

class PerfDataProto_PerfEvent;
class PerfDataProto_SampleInfo;

// Based on kernel/perf_internals.h
const size_t kBuildIDArraySize = 20;
const size_t kBuildIDStringLength = kBuildIDArraySize * 2;

// Used by malloced_unique_ptr.
struct FreeDeleter {
  inline void operator()(void* pointer) { free(pointer); }
};

// A modified version of std::unique_ptr that holds a pointer allocated by
// malloc or its cousins rather than by new. Calls free() to free the pointer
// when it is destroyed.
template <typename T>
using malloced_unique_ptr = std::unique_ptr<T, FreeDeleter>;

// Allocate |size| bytes of heap memory using calloc, returning the allocated
// memory as the variable-sized type event_t.
event_t* CallocMemoryForEvent(size_t size);

// Reallocate |event| which was previously allocated by CallocMemoryForEvent()
// to memory with a new size |new_size|.
event_t* ReallocMemoryForEvent(event_t* event, size_t new_size);

// Allocate |size| bytes of heap memory using calloc, returning the allocated
// memory as the variable-sized build ID type.
build_id_event* CallocMemoryForBuildID(size_t size);

// In perf data, strings are packed into the smallest number of 8-byte blocks
// possible, including a null terminator.
// e.g.
//    "0123"                ->  5 bytes -> packed into  8 bytes
//    "0123456"             ->  8 bytes -> packed into  8 bytes
//    "01234567"            ->  9 bytes -> packed into 16 bytes
//    "0123456789abcd"      -> 15 bytes -> packed into 16 bytes
//    "0123456789abcde"     -> 16 bytes -> packed into 16 bytes
//    "0123456789abcdef"    -> 17 bytes -> packed into 24 bytes
//
// Returns the size of the 8-byte-aligned memory for storing |string|.
inline size_t GetUint64AlignedStringLength(const string& str) {
  return Align<uint64_t>(str.size() + 1);
}

// Makes |build_id| fit the perf format, by either truncating it or adding
// zeroes to the end so that it has length kBuildIDStringLength.
void PerfizeBuildIDString(string* build_id);

// Changes |build_id| to the best guess of what the build id was before going
// through perf.  Specifically, it keeps removing trailing sequences of four
// zero bytes (or eight '0' characters) until there are no more such sequences,
// or the build id would be empty if the process were repeated.
void TrimZeroesFromBuildIDString(string* build_id);

// If |event| is not of type PERF_RECORD_SAMPLE, returns the SampleInfo field
// within it. Otherwise returns nullptr.
const PerfDataProto_SampleInfo* GetSampleInfoForEvent(
    const PerfDataProto_PerfEvent& event);

// Returns the correct |sample_time_ns| field of a PerfEvent.
uint64_t GetTimeFromPerfEvent(const PerfDataProto_PerfEvent& event);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_DATA_UTILS_H_
