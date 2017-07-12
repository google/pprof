/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
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
        // This could be a hugepage mapping following a non-hugepage mapping.
        // Because it is contiguous, assume it is part of that same mapping by
        // extending the length.
        if (IsContiguousWithCombinedMapping(mmap)) {
          combined_mapping_.set_len(combined_mapping_.len() + mmap.len());
        } else {
          combined_mapping_ = mmap;
        }

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
  if (state_ == BASE_STATE) {
    return false;
  }

  return !combined_mapping_.has_len() ||
         combined_mapping_.start() + combined_mapping_.len() == mmap.start();
}

}  // namespace perftools
