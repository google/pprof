/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_CHROME_HUGE_PAGES_MAPPING_DEDUCER_H_
#define PERFTOOLS_CHROME_HUGE_PAGES_MAPPING_DEDUCER_H_

#include "base/macros.h"
#include "quipper/perf_data.pb.h"

namespace perftools {
// A state machine that tracks the order of split-up Chrome MMAPs it has seen.
// It deduces that there was a Chrome binary that was split into multiple MMAPs,
// including a huge pages mapping in the middle. The split is as follows:
// - First Chrome MMAP (optional): start=A      len=X  pgoff=0
// - Huge pages MMAP:              start=A+X    len=Y  pgoff=0    name="//anon"
// - Second Chrome MMAP:           start=A+X+Y  len=Z  pgoff=X+Y
// Here, Y=|kHugePagesSize|.
//
class ChromeHugePagesMappingDeducer {
 public:
  ChromeHugePagesMappingDeducer();

  // Pass the next MMAP into the deducer state machine.
  void ProcessMmap(const quipper::PerfDataProto_MMapEvent& mmap);

  bool CombinedMappingAvailable() const { return state_ == SECOND_CHROME_MMAP; }

  const quipper::PerfDataProto_MMapEvent& combined_mapping() const {
    return combined_mapping_;
  }

 private:
  // Resets the state machine.
  void Reset();

  // Functions used to determine whether a mapping falls into a particular part
  // of the split-mapped sequence.
  bool IsFirstChromeMmap(const quipper::PerfDataProto_MMapEvent& mmap) const;
  bool IsHugePagesMmap(const quipper::PerfDataProto_MMapEvent& mmap) const;
  bool IsSecondChromeMmap(const quipper::PerfDataProto_MMapEvent& mmap) const;

  // Checks that the mapping is contiguous with the existing |combined_mapping|,
  // if it has already been initialized.
  bool IsContiguousWithCombinedMapping(
      const quipper::PerfDataProto_MMapEvent& mmap) const;

  // States of the state machine.
  enum State {
    // No huge pages Chrome mapping encountered.
    BASE_STATE,
    // Encountered first Chrome mapping. The first Chrome mapping may not exist,
    // so this stage can be skipped if necessary.
    FIRST_CHROME_MMAP,
    // Encountered the huge pages mapping.
    HUGE_PAGES_MMAP,
    // Encountered the second Chrome mapping, after the huge pages mapping.
    SECOND_CHROME_MMAP,
  } state_;

  // A single mapping combined from the split Chrome mappings. Gets updated as
  // new mappings are processed.
  quipper::PerfDataProto_MMapEvent combined_mapping_;

  DISALLOW_COPY_AND_ASSIGN(ChromeHugePagesMappingDeducer);
};

}  // namespace perftools

#endif  // PERFTOOLS_CHROME_HUGE_PAGES_MAPPING_DEDUCER_H_
