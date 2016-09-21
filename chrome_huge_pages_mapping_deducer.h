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

#ifndef PERFTOOLS_PROFILES_CHROME_HUGE_PAGES_MAPPING_DEDUCER_H_
#define PERFTOOLS_PROFILES_CHROME_HUGE_PAGES_MAPPING_DEDUCER_H_

#include "base/macros.h"
#include "chromiumos-wide-profiling/perf_data.pb.h"

namespace perftools {

// A state machine that tracks the order of split-up Chrome MMAPs it has seen.
// It deduces that there was a Chrome binary that was split into multiple MMAPs,
// including a huge pages mapping in the middle. The split is as follows:
// - First Chrome MMAP (optional): start=A      len=X  pgoff=0
// - Huge pages MMAP:              start=A+X    len=Y  pgoff=0    name="//anon"
// - Second Chrome MMAP:           start=A+X+Y  len=Z  pgoff=X+Y
// Here, Y=|kHugePagesSize|.
//
// TODO: Delete this class when incoming perf data has already been
// pre-processed to have combined mappings.
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

#endif  // PERFTOOLS_PROFILES_CHROME_HUGE_PAGES_MAPPING_DEDUCER_H_
