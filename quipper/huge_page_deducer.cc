#include "huge_page_deducer.h"

#include "perf_data_utils.h"

using PerfEvent = quipper::PerfDataProto::PerfEvent;
using MMapEvent = quipper::PerfDataProto::MMapEvent;

namespace quipper {
namespace {

const char kAnonFilename[] = "//anon";
const size_t kHugepageSize = 1 << 21;

}  // namespace

void DeduceHugePages(RepeatedPtrField<PerfEvent>* events) {
  // |prev_event| is the last mmap_event in |events|, or |nullptr| if no
  // mmap_events have been processed.
  PerfEvent* prev_event = nullptr;

  for (int i = 0; i < events->size(); ++i) {
    PerfEvent* event = events->Mutable(i);
    if (!event->has_mmap_event()) {
      continue;
    }

    if (prev_event == nullptr) {
      prev_event = event;
      continue;
    }

    MMapEvent* mmap = event->mutable_mmap_event();
    MMapEvent* prev_mmap = prev_event->mutable_mmap_event();

    const bool pid_match = prev_mmap->pid() == mmap->pid();
    const bool tid_match = prev_mmap->tid() == mmap->tid();

    // perf attributes neighboring anonymous mappings under the nearby
    // filename rather than "//anon".
    const bool file_match = prev_mmap->filename() == mmap->filename() ||
                            prev_mmap->filename() == kAnonFilename ||
                            mmap->filename() == kAnonFilename;
    const bool address_contiguous =
        prev_mmap->start() + prev_mmap->len() == mmap->start();

    if (!(pid_match && tid_match && file_match && address_contiguous)) {
      prev_event = event;
      continue;
    }

    const bool mmap_hugepage =
        mmap->start() % kHugepageSize == 0 && mmap->len() % kHugepageSize == 0;
    // |pgoff| == 0 is suspect, as perf reports this for anonymous mappings.
    const bool before_hugepage = prev_mmap->pgoff() == 0 &&
                                 prev_mmap->start() % kHugepageSize == 0 &&
                                 prev_mmap->len() % kHugepageSize == 0;

    if (before_hugepage) {
      if (mmap->pgoff() > 0 && mmap->pgoff() >= prev_mmap->len()) {
        // Extend |pgoff| downwards, as the existing mmap event may be anonymous
        // due to |m->pgoff()| == 0.
        prev_mmap->set_pgoff(mmap->pgoff() - prev_mmap->len());
      }

      // Replace "//anon" with a regular name if possible.
      if (prev_mmap->filename() == kAnonFilename) {
        prev_event->mutable_header()->set_size(
            prev_event->header().size() +
            GetUint64AlignedStringLength(mmap->filename()) -
            GetUint64AlignedStringLength(prev_mmap->filename()));

        prev_mmap->set_filename(mmap->filename());
        prev_mmap->set_filename_md5_prefix(mmap->filename_md5_prefix());
      }
    }

    if (mmap_hugepage) {
      if (mmap->pgoff() == 0) {
        // Extend |pgoff| upwards, as the existing mmap event may be anonymous
        // due to |m->pgoff()| == 0.
        mmap->set_pgoff(prev_mmap->pgoff() + prev_mmap->len());
      }

      // Replace "//anon" with a regular name if possible.
      if (mmap->filename() == kAnonFilename) {
        event->mutable_header()->set_size(
            event->header().size() +
            GetUint64AlignedStringLength(prev_mmap->filename()) -
            GetUint64AlignedStringLength(mmap->filename()));

        mmap->set_filename(prev_mmap->filename());
        mmap->set_filename_md5_prefix(prev_mmap->filename_md5_prefix());
      }
    }

    prev_event = event;
  }
}

void CombineMappings(RepeatedPtrField<PerfEvent>* events) {
  // Combine mappings
  RepeatedPtrField<PerfEvent> new_events;
  new_events.Reserve(events->size());

  // |prev| is the index of the last mmap_event in |new_events| (or
  // |new_events.size()| if no mmap_events have been inserted yet).
  int prev = 0;
  for (int i = 0; i < events->size(); ++i) {
    PerfEvent* event = events->Mutable(i);
    if (!event->has_mmap_event()) {
      new_events.Add()->Swap(event);
      continue;
    }

    const MMapEvent& mmap = event->mmap_event();
    // Try to merge mmap with |new_events[prev]|.
    while (prev < new_events.size() && !new_events[prev].has_mmap_event()) {
      prev++;
    }

    if (prev >= new_events.size()) {
      new_events.Add()->Swap(event);
      continue;
    }

    MMapEvent* prev_mmap = new_events[prev].mutable_mmap_event();

    const bool pid_match = prev_mmap->pid() == mmap.pid();
    const bool tid_match = prev_mmap->tid() == mmap.tid();

    const bool file_match = prev_mmap->filename() == mmap.filename();
    const bool address_contiguous =
        prev_mmap->start() + prev_mmap->len() == mmap.start();
    const bool pgoff_contiguous =
        prev_mmap->pgoff() + prev_mmap->len() == mmap.pgoff();

    const bool combine_mappings =
        pid_match && tid_match && file_match &&
        address_contiguous && pgoff_contiguous;
    if (!combine_mappings) {
      new_events.Add()->Swap(event);
      prev++;
      continue;
    }

    // Combine the lengths of the two mappings.
    prev_mmap->set_len(prev_mmap->len() + mmap.len());
  }

  events->Swap(&new_events);
}

}  // namespace quipper
