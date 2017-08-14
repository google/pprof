#include "huge_page_deducer.h"

#include "perf_data_utils.h"

using PerfEvent = quipper::PerfDataProto::PerfEvent;
using MMapEvent = quipper::PerfDataProto::MMapEvent;

namespace quipper {

void CombineHugePageMappings(RepeatedPtrField<PerfEvent>* events) {
  RepeatedPtrField<PerfEvent> new_events;
  new_events.Reserve(events->size());

  // |trailing| is the index of the last mmap_event in |new_events| (or
  // |new_events.size()| if no mmap_events have been inserted yet).
  int trailing = 0;
  // |trailing_had_hugepages| tracks whether we've folded in any hugepage
  // mappings into the current event at |new_events[trailing]|.  This is
  // necessary as we may have a sequence of contiguous mappings (a, b, c) with
  // b on hugepages.  After merging a and b, the resulting intermediate state
  // (ab, c) will not be hugepage-aligned but should continue to be merged.
  bool trailing_had_hugepages = false;
  for (int i = 0; i < events->size(); ++i) {
    PerfEvent* event = events->Mutable(i);
    if (!event->has_mmap_event()) {
      new_events.Add()->Swap(event);
      continue;
    }

    const MMapEvent& mmap = event->mmap_event();
    // Try to merge mmap with |new_events[trailing]|.
    while (trailing < new_events.size() &&
           !new_events[trailing].has_mmap_event()) {
      trailing++;
    }

    if (trailing >= new_events.size()) {
      new_events.Add()->Swap(event);
      trailing_had_hugepages = false;
      continue;
    }

    const char kAnonFilename[] = "//anon";
    const size_t kHugepageSize = 1 << 21;
    MMapEvent* prev_mmap = new_events[trailing].mutable_mmap_event();

    const bool pid_match = prev_mmap->pid() == mmap.pid();
    const bool tid_match = prev_mmap->tid() == mmap.tid();
    const bool before_hugepage = prev_mmap->pgoff() == 0 &&
                                 prev_mmap->start() % kHugepageSize == 0 &&
                                 prev_mmap->len() % kHugepageSize == 0;
    const bool mmap_hugepage =
        mmap.start() % kHugepageSize == 0 && mmap.len() % kHugepageSize == 0;

    // perf attributes neighboring anonymous mappings under the nearby
    // filename rather than "//anon".
    const bool file_match = prev_mmap->filename() == mmap.filename() ||
                            prev_mmap->filename() == kAnonFilename ||
                            mmap.filename() == kAnonFilename;
    const bool address_contiguous =
        prev_mmap->start() + prev_mmap->len() == mmap.start();
    // |pgoff| == 0 is suspect, as perf reports this for anonymous mappings.
    const bool pgoff_contiguous =
        prev_mmap->pgoff() + prev_mmap->len() == mmap.pgoff() ||
        (before_hugepage && mmap.pgoff() >= prev_mmap->len()) || mmap_hugepage;

    const bool combine_mappings =
        pid_match && tid_match && file_match &&
        (trailing_had_hugepages || before_hugepage || mmap_hugepage) &&
        address_contiguous && pgoff_contiguous;
    if (!combine_mappings) {
      new_events.Add()->Swap(event);
      trailing++;
      trailing_had_hugepages = false;
      continue;
    }

    PerfEvent* prev_event = new_events.Mutable(trailing);

    if (prev_mmap->pgoff() == 0 && mmap.pgoff() > 0 &&
        mmap.pgoff() >= prev_mmap->len()) {
      // Extend |pgoff| downwards, as the existing mmap event may be anonymous
      // due to |m->pgoff()| == 0.
      prev_mmap->set_pgoff(mmap.pgoff() - prev_mmap->len());
    }

    // Combine the lengths of the two mappings.
    prev_mmap->set_len(prev_mmap->len() + mmap.len());

    // Replace "//anon" with a regular name if possible.
    if (prev_mmap->filename() == kAnonFilename) {
      prev_event->mutable_header()->set_size(
          prev_event->header().size() +
          GetUint64AlignedStringLength(mmap.filename()) -
          GetUint64AlignedStringLength(prev_mmap->filename()));

      prev_mmap->set_filename(mmap.filename());
      prev_mmap->set_filename_md5_prefix(mmap.filename_md5_prefix());
    }

    trailing_had_hugepages = true;
  }

  events->Swap(&new_events);
}

}  // namespace quipper
