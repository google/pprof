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

#include <stddef.h>

#include "chrome_huge_pages_mapping_deducer.h"

namespace perftools {

namespace {

using quipper::PerfDataProto_MMapEvent;

// Name of Chrome binary.
const char kChromeFilename[] = "/opt/google/chrome/chrome";
// Filename of Chrome huge pages mapping.
const char kHugePagesFilename[] = "//anon";
// Size in bytes of a huge page. A huge pages mapping could be a multiple of
// this value.
const size_t kHugePageSize = 2 * 1024 * 1024;

}  // namespace

ChromeHugePagesMappingDeducer::ChromeHugePagesMappingDeducer()
    : state_(BASE_STATE) {}

void ChromeHugePagesMappingDeducer::ProcessMmap(
    const PerfDataProto_MMapEvent& mmap) {
  switch (state_) {
    case BASE_STATE:
    case SECOND_CHROME_MMAP:  // After the second Chrome mapping been processed,
                              // a new huge pages mapping could come up
                              // immediately. Otherwise, reset.
      if (IsFirstChromeMmap(mmap)) {
        combined_mapping_ = mmap;
        state_ = FIRST_CHROME_MMAP;
      } else if (IsHugePagesMmap(mmap)) {
        combined_mapping_ = mmap;
        // Skipping the first Chrome mapping so fill in the name manually.
        combined_mapping_.set_filename(kChromeFilename);
        state_ = HUGE_PAGES_MMAP;
      } else {
        Reset();
      }
      break;
    case FIRST_CHROME_MMAP:
      // This is the case where there is already a Chrome mapping, so make sure
      // that the new mapping is contiguous.
      if (IsHugePagesMmap(mmap) && IsContiguousWithCombinedMapping(mmap)) {
        combined_mapping_.set_len(combined_mapping_.len() + mmap.len());
        state_ = HUGE_PAGES_MMAP;
      } else {
        Reset();
      }
      break;
    case HUGE_PAGES_MMAP:
      if (IsSecondChromeMmap(mmap)) {
        combined_mapping_.set_pgoff(mmap.pgoff() - combined_mapping_.len());
        combined_mapping_.set_len(combined_mapping_.len() + mmap.len());
        state_ = SECOND_CHROME_MMAP;
      } else {
        Reset();
      }
      break;
    default:
      Reset();
  }
}

void ChromeHugePagesMappingDeducer::Reset() {
  state_ = BASE_STATE;
  combined_mapping_.Clear();
}

bool ChromeHugePagesMappingDeducer::IsFirstChromeMmap(
    const PerfDataProto_MMapEvent& mmap) const {
  return mmap.filename() == kChromeFilename && mmap.pgoff() == 0;
}

bool ChromeHugePagesMappingDeducer::IsHugePagesMmap(
    const PerfDataProto_MMapEvent& mmap) const {
  if (mmap.filename() != kHugePagesFilename) {
    return false;
  }
  // Even though the original mapping is huge-page aligned, the perf data could
  // have been post-processed to the point where it is no longer aligned.
  return mmap.len() % kHugePageSize == 0 && mmap.pgoff() == 0;
}

bool ChromeHugePagesMappingDeducer::IsSecondChromeMmap(
    const PerfDataProto_MMapEvent& mmap) const {
  if (mmap.filename() != kChromeFilename) {
    return false;
  }
  // The second Chrome mapping's pgoff must be equal to the sum of the size of
  // the previous two mappings. The first Chrome mapping could be missing so the
  // pgoff could be larger than the total mapping size so far.
  return mmap.pgoff() >= combined_mapping_.pgoff() &&
         combined_mapping_.start() + combined_mapping_.len() == mmap.start();
}

bool ChromeHugePagesMappingDeducer::IsContiguousWithCombinedMapping(
    const PerfDataProto_MMapEvent& mmap) const {
  return !combined_mapping_.has_len() ||
         combined_mapping_.start() + combined_mapping_.len() == mmap.start();
}

}  // namespace perftools
