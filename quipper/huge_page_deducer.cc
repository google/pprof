#include "huge_page_deducer.h"

#include <limits>

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

// IsContiguous returns true if mmap |a| is immediately followed by |b|
// within a process' address space.
bool IsContiguous(const MMapEvent& a, const MMapEvent& b) {
  return a.pid() == b.pid() && (a.start() + a.len()) == b.start();
}

// IsEquivalentFile returns true iff |a| and |b| have the same name, or if
// either of them are anonymous memory (and thus likely to be a --hugepage_text
// version of the same file).
bool IsEquivalentFile(const MMapEvent& a, const MMapEvent& b) {
  // perf attributes neighboring anonymous mappings under the argv[0]
  // filename rather than "//anon", so check filename equality, as well as
  // anonymous.
  return a.filename() == b.filename() || IsAnon(a) || IsAnon(b);
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

namespace {

// MMapRange represents an index into a PerfEvents sequence that contains
// a contiguous region of mmaps that have all of the same filename and pgoff.
class MMapRange {
 public:
  // Default constructor is an invalid range.
  MMapRange()
      : first_(std::numeric_limits<int>::max()),
        last_(std::numeric_limits<int>::min()) {}

  // Construct a real range.
  MMapRange(int first_index, int last_index)
      : first_(first_index), last_(last_index) {}

  uint64 Len(const RepeatedPtrField<PerfEvent>& events) const {
    auto& first = events.Get(first_).mmap_event();
    auto& last = events.Get(last_).mmap_event();
    return last.start() - first.start() + last.len();
  }

  int FirstIndex() const { return first_; }
  int LastIndex() const { return last_; }
  bool IsValid() const { return first_ <= last_; }

  const MMapEvent& FirstMmap(const RepeatedPtrField<PerfEvent>& events) const {
    return events.Get(first_).mmap_event();
  }

  const MMapEvent& LastMmap(const RepeatedPtrField<PerfEvent>& events) const {
    return events.Get(last_).mmap_event();
  }

 private:
  int first_;
  int last_;
};

std::ostream& operator<<(std::ostream& os, const MMapRange& r) {
  os << "[" << r.FirstIndex() << "," << r.LastIndex() << "]";
  return os;
}

// MMapRange version of IsContiguous(MMapEvent, MMapEvent).
bool IsContiguous(const RepeatedPtrField<PerfEvent>& events, const MMapRange& a,
                  const MMapRange& b) {
  return IsContiguous(a.LastMmap(events), b.FirstMmap(events));
}

// MMapRange version of IsIsEquivalent(MMapEvent, MMapEvent).
bool IsEquivalentFile(const RepeatedPtrField<PerfEvent>& events,
                      const MMapRange& a, const MMapRange& b) {
  // Because a range has the same file for all mmaps within it, assume that
  // checking any mmap in |a| with any in |b| is sufficient.
  return IsEquivalentFile(a.LastMmap(events), b.FirstMmap(events));
}

// FindRange returns a MMapRange of contiguous MmapEvents that:
// - either:
//   - contains 1 or more MmapEvents with pgoff == 0
//   - is a single MmapEvent with pgoff != 0
// - and:
//   - has the same filename for all entries
// Otherwise, if none can be found, an invalid range will be returned.
MMapRange FindRange(const RepeatedPtrField<PerfEvent>& events, int start) {
  const MMapEvent* prev_mmap = nullptr;
  MMapRange range;
  for (int i = start; i < events.size(); i++) {
    const PerfEvent& event = events.Get(i);
    // Skip irrelevant events
    if (!event.has_mmap_event()) {
      continue;
    }
    // Skip dynamic mmap() events. Hugepage deduction only works on mmaps as
    // synthesized by perf from /proc/${pid}/maps, which have timestamp==0.
    // Support for deducing hugepages from a sequence of mmap()/mremap() calls
    // would require additional deduction logic.
    if (event.timestamp() != 0) {
      continue;
    }
    const MMapEvent& mmap = events.Get(i).mmap_event();
    if (prev_mmap == nullptr) {
      range = MMapRange(i, i);
      prev_mmap = &mmap;
    }
    // Ranges match exactly: //anon,//anon, or file,file; If they use different
    // names, then deduction needs to consider them independently.
    if (prev_mmap->filename() != mmap.filename()) {
      break;
    }
    // If they're not virtually contiguous, they're not a single range.
    if (start != i && !IsContiguous(*prev_mmap, mmap)) {
      break;
    }
    // If this segment has a page offset, assume that it is *not* hugepage
    // backed, and thus does not need separate deduction.
    if (mmap.pgoff() != 0) {
      break;
    }
    CHECK(mmap.pgoff() == 0 || !IsAnon(mmap))
        << "Anonymous pages can't have pgoff set";
    prev_mmap = &mmap;
    range = MMapRange(range.FirstIndex(), i);
  }
  // Range has:
  // - single file
  // - virtually contiguous
  // - either: is multiple mappings *or* has pgoff=0
  return range;
}

// FindNextRange will return the next range after the given |prev_range| if
// there is one; otherwise it will return an invalid range.
MMapRange FindNextRange(const RepeatedPtrField<PerfEvent>& events,
                        const MMapRange& prev_range) {
  MMapRange ret;
  if (prev_range.IsValid() && prev_range.LastIndex() < events.size()) {
    ret = FindRange(events, prev_range.LastIndex() + 1);
  }
  return ret;
}

// UpdateRangeFromNext will set the filename / pgoff of all mmaps within |range|
// to be pgoff-contiguous with |next_range|, and match its file information.
void UpdateRangeFromNext(const MMapRange& range, const MMapRange& next_range,
                         RepeatedPtrField<PerfEvent>* events) {
  CHECK(range.LastIndex() < events->size());
  CHECK(next_range.LastIndex() < events->size());
  const MMapEvent& src = next_range.FirstMmap(*events);
  const uint64 start_pgoff = src.pgoff() - range.Len(*events);
  uint64 pgoff = start_pgoff;
  for (int i = range.FirstIndex(); i <= range.LastIndex(); i++) {
    if (!events->Get(i).has_mmap_event()) {
      continue;
    }
    PerfEvent* event = events->Mutable(i);
    MMapEvent* mmap = event->mutable_mmap_event();

    // Replace "//anon" with a regular name if possible.
    if (IsAnon(*mmap)) {
      CHECK_EQ(mmap->pgoff(), 0) << "//anon should have offset=0 for mmap"
                                 << event->ShortDebugString();
      SetMmapFilename(event, src.filename(), src.filename_md5_prefix());
    }

    if (mmap->pgoff() == 0) {
      mmap->set_pgoff(pgoff);
      if (src.has_maj()) {
        mmap->set_maj(src.maj());
      }
      if (src.has_min()) {
        mmap->set_min(src.min());
      }
      if (src.has_ino()) {
        mmap->set_ino(src.ino());
      }
      if (src.has_ino_generation()) {
        mmap->set_ino_generation(src.ino_generation());
      }
    }
    pgoff += mmap->len();
  }
  CHECK_EQ(pgoff, start_pgoff + range.Len(*events));
}
}  // namespace

void DeduceHugePages(RepeatedPtrField<PerfEvent>* events) {
  // |prev_range|, if IsValid(), represents the preview mmap range seen (and
  // already processed / updated).
  MMapRange prev_range;
  // |range| contains the currently-being-processed mmap range, which will have
  // its hugepages ranges deduced.
  MMapRange range = FindRange(*events, 0);
  // |next_range| contains the next range to process, possibily containing
  // pgoff != 0 or !IsAnon(filename) from which the current range can be
  // updated.
  MMapRange next_range = FindNextRange(*events, range);

  for (; range.IsValid(); prev_range = range, range = next_range,
                          next_range = FindNextRange(*events, range)) {
    const bool have_next =
        (next_range.IsValid() && IsContiguous(*events, range, next_range) &&
         IsEquivalentFile(*events, range, next_range));

    // If there's no mmap after this, then we assume that this is *not* viable
    // a hugepage_text mapping. This is true unless we're really unlucky. If:
    // - the binary is mapped such that the limit is hugepage aligned
    //   (presumably 4Ki/2Mi chance == p=0.03125)
    // - and the entire binaryis hugepage_text mapped
    if (!have_next) {
      continue;
    }

    const bool have_prev =
        (prev_range.IsValid() && IsContiguous(*events, prev_range, range) &&
         IsEquivalentFile(*events, prev_range, range) &&
         IsEquivalentFile(*events, prev_range, next_range));

    uint64 start_pgoff = 0;
    if (have_prev) {
      const auto& prev = prev_range.LastMmap(*events);
      start_pgoff = prev.pgoff() + prev.len();
    }
    const auto& next = next_range.FirstMmap(*events);
    // prev.pgoff should be valid now, so let's double-check that
    // if next has a non-zero pgoff, that {prev,curr,next} will have
    // contiguous pgoff once updated.
    if (next.pgoff() >= range.Len(*events) &&
        (next.pgoff() - range.Len(*events)) == start_pgoff) {
      UpdateRangeFromNext(range, next_range, events);
    }
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
    while (prev < new_events.size() && !new_events.Get(prev).has_mmap_event()) {
      prev++;
    }

    if (prev >= new_events.size()) {
      new_events.Add()->Swap(event);
      continue;
    }

    MMapEvent* prev_mmap = new_events.Mutable(prev)->mutable_mmap_event();

    // Don't use IsEquivalentFile(); we don't want to combine //anon with
    // files if DeduceHugepages didn't already fix up the mappings.
    const bool file_match = prev_mmap->filename() == mmap.filename();
    const bool pgoff_contiguous =
        file_match && (prev_mmap->pgoff() + prev_mmap->len() == mmap.pgoff());

    const bool combine_mappings =
        IsContiguous(*prev_mmap, mmap) && pgoff_contiguous;
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
