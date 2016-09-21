// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_HUGE_PAGES_MAPPING_DEDUCER_H_
#define CHROMIUMOS_WIDE_PROFILING_HUGE_PAGES_MAPPING_DEDUCER_H_

#include "base/macros.h"
#include "chromiumos-wide-profiling/compat/proto.h"
#include "chromiumos-wide-profiling/compat/string.h"

namespace quipper {

// A state machine that tracks the order of split-up MMAPs it has seen. It
// deduces that there was a binary that was split into multiple MMAPs, including
// a huge pages mapping in the middle. The split is as follows:
// - First normal MMAP*    : start=A      len=X  pgoff=0    name=|filename_|
// - Huge pages MMAP       : start=A+X    len=Y  pgoff=0    name="//anon"
// - Second normal MMAP    : start=A+X+Y  len=Z  pgoff=X+Y  name=|filename_|
// Here, |Y| is a multiple of the size of a huge page (2 MB).
// *The first mapping is optional. The filename would have to be deduced from
// the second mapping in that case.
class HugePagesMappingDeducer {
 public:
  explicit HugePagesMappingDeducer(const string& filename);

  // Pass the next MMAP into the deducer state machine.
  void ProcessMmap(const PerfDataProto_MMapEvent& mmap);

  // This returns true once ProcessMmap() has been called on all of the mappings
  // in the split mapping of a huge pages binary. When this returns true, call
  // combined_mapping() to get the actual combined mapping. Calling
  // ProcessMmap() again will invalidate |combined_mapping_| and cause this
  // function to return false.
  bool CombinedMappingAvailable() const {
    return state_ == SECOND_NORMAL_MMAP;
  }

  const PerfDataProto_MMapEvent& combined_mapping() const {
    return combined_mapping_;
  }

 private:
  // Resets the state machine.
  void Reset();

  // Functions used to determine whether a mapping falls into a particular part
  // of the split-mapped sequence.
  bool IsFirstNormalMmap(const PerfDataProto_MMapEvent& mmap) const;
  bool IsHugePagesMmap(const PerfDataProto_MMapEvent& mmap) const;
  bool IsSecondNormalMmap(const PerfDataProto_MMapEvent& mmap) const;

  // Checks that the mapping is contiguous with the existing |combined_mapping|,
  // if it has already been initialized.
  bool IsContiguousWithCombinedMapping(
      const PerfDataProto_MMapEvent& mmap) const;

  // States of the state machine.
  enum State {
    // No huge pages Chrome mapping encountered.
    BASE_STATE,
    // Encountered first mapping, which is a normal mapping.
    FIRST_NORMAL_MMAP,
    // Encountered the huge pages mapping.
    HUGE_PAGES_MMAP,
    // Encountered the second non-hugepages mapping, after the huge pages
    // mapping.
    SECOND_NORMAL_MMAP,
  } state_;

  // The name of the binary that was mapped with huge pages.
  string filename_;

  // A single mapping combined from the split mappings. Gets updated as new
  // mappings are processed.
  PerfDataProto_MMapEvent combined_mapping_;

  DISALLOW_COPY_AND_ASSIGN(HugePagesMappingDeducer);
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_HUGE_PAGES_MAPPING_DEDUCER_H_
