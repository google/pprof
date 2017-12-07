#include "huge_page_deducer.h"

#include "perf_data_utils.h"

#include "base/logging.h"

using PerfEvent = quipper::PerfDataProto::PerfEvent;
using MMapEvent = quipper::PerfDataProto::MMapEvent;

namespace quipper {
namespace {

const char kAnonFilename[] = "//anon";
const size_t kHugepageSize = 1 << 21;

bool IsAnon(const MMapEvent& event) {
  return event.filename() == kAnonFilename;
}

// Helper to correctly update a filename on a PerfEvent that contains an
// MMapEvent.
void SetMmapFilename(PerfEvent* event, const string& new_filename,
                     uint64_t new_filename_md5_prefix) {
  CHECK(event->has_mmap_event());
  event->mutable_header()->set_size(
      event->header().size() + GetUint64AlignedStringLength(new_filename) -
      GetUint64AlignedStringLength(event->mmap_event().filename()));

  event->mutable_mmap_event()->set_filename(new_filename);
  event->mutable_mmap_event()->set_filename_md5_prefix(new_filename_md5_prefix);
}
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

    // perf attributes neighboring anonymous mappings under the nearby
    // filename rather than "//anon".
    const bool file_match = prev_mmap->filename() == mmap->filename() ||
                            IsAnon(*prev_mmap) || IsAnon(*mmap);
    const bool address_contiguous =
        pid_match && (prev_mmap->start() + prev_mmap->len() == mmap->start());

    if (!(file_match && address_contiguous)) {
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
      if (IsAnon(*prev_mmap)) {
        SetMmapFilename(prev_event, mmap->filename(),
                        mmap->filename_md5_prefix());
      }
    }

    if (mmap_hugepage) {
      if (mmap->pgoff() == 0) {
        // Extend |pgoff| upwards, as the existing mmap event may be anonymous
        // due to |m->pgoff()| == 0.
        mmap->set_pgoff(prev_mmap->pgoff() + prev_mmap->len());
      }

      // Replace "//anon" with a regular name if possible.
      if (IsAnon(*mmap)) {
        SetMmapFilename(event, prev_mmap->filename(),
                        prev_mmap->filename_md5_prefix());
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
    const bool file_match = prev_mmap->filename() == mmap.filename();
    const bool address_contiguous =
        pid_match && (prev_mmap->start() + prev_mmap->len() == mmap.start());
    const bool pgoff_contiguous =
        file_match && (prev_mmap->pgoff() + prev_mmap->len() == mmap.pgoff());

    const bool combine_mappings = address_contiguous && pgoff_contiguous;
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
