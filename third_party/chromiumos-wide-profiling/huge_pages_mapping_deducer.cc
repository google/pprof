// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "chromiumos-wide-profiling/huge_pages_mapping_deducer.h"

namespace quipper {

namespace {

// Filename of a huge pages mapping.
const char kHugePagesFilename[] = "//anon";
// Size in bytes of a huge page. A huge pages mapping could be a multiple of
// this value.
const size_t kHugePageSize = 2 * 1024 * 1024;

}  // namespace

HugePagesMappingDeducer::HugePagesMappingDeducer(const string& filename)
    : state_(BASE_STATE),
      filename_(filename) {}

void HugePagesMappingDeducer::ProcessMmap(
      const PerfDataProto_MMapEvent& mmap) {
  switch (state_) {
    case BASE_STATE:
    case SECOND_NORMAL_MMAP:  // After the second normal mapping been processed,
                              // a new huge pages mapping could come up
                              // immediately. Otherwise, reset.
      if (IsFirstNormalMmap(mmap)) {
        combined_mapping_ = mmap;
        state_ = FIRST_NORMAL_MMAP;
      } else if (IsHugePagesMmap(mmap)) {
        combined_mapping_ = mmap;
        // The first normal mapping is not present. Fill in the name manually.
        combined_mapping_.set_filename(filename_);
        state_ = HUGE_PAGES_MMAP;
      } else {
        Reset();
      }
      break;
    case FIRST_NORMAL_MMAP:
      // This is the case where there is already a normal mapping, so make sure
      // that the new mapping is contiguous.
      if (IsHugePagesMmap(mmap) && IsContiguousWithCombinedMapping(mmap)) {
        combined_mapping_.set_len(combined_mapping_.len() + mmap.len());
        state_ = HUGE_PAGES_MMAP;
      } else {
        Reset();
      }
      break;
    case HUGE_PAGES_MMAP:
      if (IsSecondNormalMmap(mmap)) {
        combined_mapping_.set_pgoff(mmap.pgoff() - combined_mapping_.len());
        combined_mapping_.set_len(combined_mapping_.len() + mmap.len());
        state_ = SECOND_NORMAL_MMAP;
      } else {
        Reset();
      }
      break;
  }
}

void HugePagesMappingDeducer::Reset() {
  state_ = BASE_STATE;
  combined_mapping_.Clear();
}

bool HugePagesMappingDeducer::IsFirstNormalMmap(
    const PerfDataProto_MMapEvent& mmap) const {
  return mmap.filename() == filename_ && mmap.pgoff() == 0;
}

bool HugePagesMappingDeducer::IsHugePagesMmap(
    const PerfDataProto_MMapEvent& mmap) const {
  return mmap.filename() == kHugePagesFilename &&
         // Even though the original mapping is huge-page aligned, the perf data
         // could have been post-processed to the point where it is no longer
         // aligned.
         mmap.len() % kHugePageSize == 0 &&
         mmap.pgoff() == 0;
}

bool HugePagesMappingDeducer::IsSecondNormalMmap(
    const PerfDataProto_MMapEvent& mmap) const {
  return mmap.filename() == filename_ &&
         // The second normal mapping's pgoff must be equal to the total size of
         // the mapping(s) so far.
         mmap.pgoff() == combined_mapping_.len() &&
         combined_mapping_.start() + combined_mapping_.len() == mmap.start();
}

bool HugePagesMappingDeducer::IsContiguousWithCombinedMapping(
    const PerfDataProto_MMapEvent& mmap) const {
  return !combined_mapping_.has_len() ||
         combined_mapping_.start() + combined_mapping_.len() == mmap.start();
}

}  // namespace quipper
