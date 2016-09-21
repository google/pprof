// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/perf_parser.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>

#include "base/logging.h"

#include "chromiumos-wide-profiling/address_mapper.h"
#include "chromiumos-wide-profiling/compat/proto.h"
#include "chromiumos-wide-profiling/compat/string.h"
#include "chromiumos-wide-profiling/dso.h"
#include "chromiumos-wide-profiling/huge_pages_mapping_deducer.h"
#include "chromiumos-wide-profiling/utils.h"

namespace quipper {

using BranchStackEntry = PerfDataProto_BranchStackEntry;
using CommEvent = PerfDataProto_CommEvent;
using ForkEvent = PerfDataProto_ForkEvent;
using MMapEvent = PerfDataProto_MMapEvent;
using PerfEvent = PerfDataProto_PerfEvent;
using SampleEvent = PerfDataProto_SampleEvent;

namespace {

// MMAPs are aligned to pages of this many bytes.
const uint64_t kMmapPageAlignment = sysconf(_SC_PAGESIZE);

// Name and ID of the kernel swapper process.
const char kSwapperCommandName[] = "swapper";
const uint32_t kSwapperPid = 0;

// Name of Chrome binary.
const char kChromeFilename[] = "/opt/google/chrome/chrome";

// Returns the offset within a page of size |kMmapPageAlignment|, given an
// address. Requires that |kMmapPageAlignment| be a power of 2.
uint64_t GetPageAlignedOffset(uint64_t addr) {
  return addr % kMmapPageAlignment;
}

// Returns true if |e1| has an earlier timestamp than |e2|. Used to sort an
// array of events.
bool CompareParsedEventTimes(const ParsedEvent& e1, const ParsedEvent& e2) {
  return (GetTimeFromPerfEvent(*e1.event_ptr) <
          GetTimeFromPerfEvent(*e2.event_ptr));
}

bool IsNullBranchStackEntry(const BranchStackEntry& entry) {
  return (!entry.from_ip() && !entry.to_ip());
}

// Walks through all the perf events in |*reader| and searches for split
// mappings due to huge pages. Combines these split mappings into one and
// replaces the split mapping events. Modifies the events vector stored in
// |*reader|.
//
// This may not correctly handle perf data that has been processed to remove
// MMAPs that contain no sample events, since one or more of the mappings
// necessary to resolve the huge pages mapping could have been discarded. The
// result would be that the huge pages mapping would remain as "//anon" and the
// other mappings would remain unchanged.
void CombineHugePagesMappings(PerfReader* reader) {
    std::map<uint32_t, std::unique_ptr<HugePagesMappingDeducer>>
        pids_to_deducers;
    RepeatedPtrField<PerfEvent> new_events;
    new_events.Reserve(reader->events().size());

    for (int i = 0; i < reader->events().size(); ++i) {
      PerfEvent* event = reader->mutable_events()->Mutable(i);
      if (!event->has_mmap_event()) {
        new_events.Add()->Swap(event);
        continue;
      }

      const MMapEvent& mmap = event->mmap_event();
      // Create a new huge pages mapping deducer for the process if necessary.
      auto& deducer = pids_to_deducers[mmap.pid()];
      if (!deducer) {
        deducer.reset(new HugePagesMappingDeducer(kChromeFilename));
      }
      deducer->ProcessMmap(mmap);
      if (!deducer->CombinedMappingAvailable()) {
        new_events.Add()->Swap(event);
        continue;
      }

      // When there is a newly combined mapping, walk backwards to find the
      // existing mappings and replace them with the combined mapping. Assumes
      // that within the events vector, the split Chrome mappings appear
      // consecutively, with no other mappings in between.
      const MMapEvent& combined_mmap = deducer->combined_mapping();
      for (int j = new_events.size() - 1;
           j >= 0 && new_events.Get(j).has_mmap_event();
           --j) {
        MMapEvent* old_mmap = new_events.Mutable(j)->mutable_mmap_event();
        if (old_mmap->start() == combined_mmap.start()) {
          // Delete all split mappings except the first one...
          new_events.DeleteSubrange(j + 1, new_events.size() - j - 1);
          // ... and update it with the new combined mapping info.
          *old_mmap = combined_mmap;
          break;
        }
      }
    }

    reader->mutable_events()->Swap(&new_events);
}

}  // namespace

PerfParser::PerfParser(PerfReader* reader) : reader_(reader) {}

PerfParser::~PerfParser() {}

PerfParser::PerfParser(PerfReader* reader, const PerfParserOptions& options)
    : reader_(reader),
      options_(options) {}

bool PerfParser::ParseRawEvents() {
  // Just in case there was data from a previous call.
  process_mappers_.clear();

  // Find and combine split huge pages mappings.
  if (options_.combine_huge_pages_mappings) {
    CombineHugePagesMappings(reader_);
  }

  // Clear the parsed events to reset their fields. Otherwise, non-sample events
  // may have residual DSO+offset info.
  parsed_events_.clear();

  // Events of type PERF_RECORD_FINISHED_ROUND don't have a timestamp, and are
  // not needed.
  // TODO(dhsharp): Follow the pattern of perf's util/ordered_events to
  // use the partial-sorting of events between rounds to sort faster.
  parsed_events_.resize(reader_->events().size());
  size_t write_index = 0;
  for (int i = 0; i < reader_->events().size(); ++i) {
    if (reader_->events().Get(i).header().type() == PERF_RECORD_FINISHED_ROUND)
      continue;
    parsed_events_[write_index++].event_ptr =
        reader_->mutable_events()->Mutable(i);
  }
  parsed_events_.resize(write_index);

  MaybeSortParsedEvents();
  ProcessEvents();

  if (!options_.discard_unused_events)
    return true;

  // Some MMAP/MMAP2 events' mapped regions will not have any samples. These
  // MMAP/MMAP2 events should be dropped. |parsed_events_| should be
  // reconstructed without these events.
  write_index = 0;
  size_t read_index;
  for (read_index = 0; read_index < parsed_events_.size(); ++read_index) {
    const ParsedEvent& event = parsed_events_[read_index];
    if (event.event_ptr->has_mmap_event() &&
        event.num_samples_in_mmap_region == 0) {
      continue;
    }
    if (read_index != write_index)
      parsed_events_[write_index] = event;
    ++write_index;
  }
  CHECK_LE(write_index, parsed_events_.size());
  parsed_events_.resize(write_index);

  // Update the events in |reader_| to match the updated events.
  UpdatePerfEventsFromParsedEvents();

  return true;
}

bool PerfParser::ProcessEvents() {
  stats_ = {0};

  stats_.did_remap = false;   // Explicitly clear the remap flag.

  // Pid 0 is called the swapper process. Even though perf does not record a
  // COMM event for pid 0, we act like we did receive a COMM event for it. Perf
  // does this itself, example:
  //   http://lxr.free-electrons.com/source/tools/perf/util/session.c#L1120
  commands_.insert(kSwapperCommandName);
  pidtid_to_comm_map_[std::make_pair(kSwapperPid, kSwapperPid)] =
      &(*commands_.find(kSwapperCommandName));

  // NB: Not necessarily actually sorted by time.
  for (size_t i = 0; i < parsed_events_.size(); ++i) {
    ParsedEvent& parsed_event = parsed_events_[i];
    PerfEvent& event = *parsed_event.event_ptr;
    switch (event.header().type()) {
      case PERF_RECORD_SAMPLE:
        // SAMPLE doesn't have any fields to log at a fixed,
        // previously-endian-swapped location. This used to log ip.
        VLOG(1) << "SAMPLE";
        ++stats_.num_sample_events;
        if (MapSampleEvent(&parsed_event))
          ++stats_.num_sample_events_mapped;
        break;
      case PERF_RECORD_MMAP:
      case PERF_RECORD_MMAP2:
      {
        const char* mmap_type_name =
            event.header().type() == PERF_RECORD_MMAP ? "MMAP" : "MMAP2";
        VLOG(1) << mmap_type_name << ": " << event.mmap_event().filename();
        ++stats_.num_mmap_events;
        // Use the array index of the current mmap event as a unique identifier.
        CHECK(MapMmapEvent(event.mutable_mmap_event(), i))
            << "Unable to map " << mmap_type_name << " event!";
        // No samples in this MMAP region yet, hopefully.
        parsed_event.num_samples_in_mmap_region = 0;
        DSOInfo dso_info;
        dso_info.name = event.mmap_event().filename();
        if (event.header().type() == PERF_RECORD_MMAP2) {
          dso_info.maj = event.mmap_event().maj();
          dso_info.min = event.mmap_event().min();
          dso_info.ino = event.mmap_event().ino();
        }
        name_to_dso_.emplace(dso_info.name, dso_info);
        break;
      }
      case PERF_RECORD_FORK:
        VLOG(1) << "FORK: " << event.fork_event().ppid()
                << ":" << event.fork_event().ptid()
                << " -> " << event.fork_event().pid()
                << ":" << event.fork_event().tid();
        ++stats_.num_fork_events;
        CHECK(MapForkEvent(event.fork_event())) << "Unable to map FORK event!";
        break;
      case PERF_RECORD_EXIT:
        // EXIT events have the same structure as FORK events.
        VLOG(1) << "EXIT: " << event.fork_event().ppid()
                << ":" << event.fork_event().ptid();
        ++stats_.num_exit_events;
        break;
      case PERF_RECORD_COMM:
      {
        VLOG(1) << "COMM: " << event.comm_event().pid()
                << ":" << event.comm_event().tid() << ": "
                << event.comm_event().comm();
        ++stats_.num_comm_events;
        CHECK(MapCommEvent(event.comm_event()));
        commands_.insert(event.comm_event().comm());
        const PidTid pidtid = std::make_pair(event.comm_event().pid(),
                                             event.comm_event().tid());
        pidtid_to_comm_map_[pidtid] =
            &(*commands_.find(event.comm_event().comm()));
        break;
      }
      case PERF_RECORD_LOST:
      case PERF_RECORD_THROTTLE:
      case PERF_RECORD_UNTHROTTLE:
      case PERF_RECORD_READ:
      case PERF_RECORD_MAX:
        VLOG(1) << "Parsed event type: " << event.header().type()
                << ". Doing nothing.";
        break;
      default:
        LOG(ERROR) << "Unknown event type: " << event.header().type();
        return false;
    }
  }
  if (!FillInDsoBuildIds())
    return false;

  // Print stats collected from parsing.
  LOG(INFO) << "Parser processed: "
            << stats_.num_mmap_events << " MMAP/MMAP2 events, "
            << stats_.num_comm_events << " COMM events, "
            << stats_.num_fork_events << " FORK events, "
            << stats_.num_exit_events << " EXIT events, "
            << stats_.num_sample_events << " SAMPLE events, "
            << stats_.num_sample_events_mapped << " of these were mapped";

  float sample_mapping_percentage =
      static_cast<float>(stats_.num_sample_events_mapped) /
      stats_.num_sample_events * 100.;
  float threshold = options_.sample_mapping_percentage_threshold;
  if (sample_mapping_percentage < threshold) {
    LOG(ERROR) << "Mapped " << static_cast<int>(sample_mapping_percentage)
               << "% of samples, expected at least "
               << static_cast<int>(threshold) << "%";
    return false;
  }
  stats_.did_remap = options_.do_remap;
  return true;
}

namespace {

class FdCloser {
 public:
  explicit FdCloser(int fd) : fd_(fd) {}
  ~FdCloser() { if (fd_ != -1) close(fd_); }
 private:
  FdCloser() = delete;
  FdCloser(FdCloser&) = delete;

  int fd_;
};

bool ReadElfBuildIdIfSameInode(const string& dso_path, const DSOInfo& dso,
                               string* buildid) {
  int fd = open(dso_path.c_str(), O_RDONLY);
  FdCloser fd_closer(fd);
  if (fd == -1) {
    if (errno != ENOENT)
      LOG(ERROR) << "Failed to open ELF file: " << dso_path;
    return false;
  }

  struct stat s;
  CHECK_GE(fstat(fd, &s), 0);
  // Only reject based on inode if we actually have device info (from MMAP2).
  if (dso.maj != 0 && dso.min != 0 && !SameInode(dso, &s))
    return false;

  return ReadElfBuildId(fd, buildid);
}

// Looks up build ID of a given DSO by reading directly from the file system.
// - Does not support reading build ID of the main kernel binary.
// - Reads build IDs of kernel modules and other DSOs using functions in dso.h.
string FindDsoBuildId(const DSOInfo& dso_info) {
  string buildid_bin;
  const string& dso_name = dso_info.name;
  if (IsKernelNonModuleName(dso_name))
    return buildid_bin;  // still empty
  // Does this look like a kernel module?
  if (dso_name.size() >= 2 && dso_name[0] == '[' && dso_name.back() == ']') {
    // This may not be successful, but either way, just return. buildid_bin
    // will be empty if the module was not found.
    ReadModuleBuildId(dso_name.substr(1, dso_name.size() - 2),
                      &buildid_bin);
    return buildid_bin;
  }
  // Try normal files, possibly inside containers.
  u32 last_pid = 0;
  for (PidTid pidtid : dso_info.threads) {
    u32 pid, tid;
    std::tie(pid, tid) = pidtid;
    stringstream dso_path_stream;
    dso_path_stream << "/proc/" << tid << "/root/" << dso_name;
    string dso_path = dso_path_stream.str();
    if (ReadElfBuildIdIfSameInode(dso_path, dso_info, &buildid_bin)) {
      return buildid_bin;
    }
    // Avoid re-trying the parent process if it's the same for multiple threads.
    // dso_info.threads is sorted, so threads in a process should be adjacent.
    if (pid == last_pid || pid == tid)
      continue;
    last_pid = pid;
    // Try the parent process:
    stringstream parent_dso_path_stream;
    parent_dso_path_stream << "/proc/" << pid << "/root/" << dso_name;
    string parent_dso_path = parent_dso_path_stream.str();
    if (ReadElfBuildIdIfSameInode(parent_dso_path, dso_info, &buildid_bin)) {
      return buildid_bin;
    }
  }
  // Still don't have a buildid. Try our own filesystem:
  if (ReadElfBuildIdIfSameInode(dso_name, dso_info, &buildid_bin)) {
    return buildid_bin;
  }
  return buildid_bin;  // still empty.
}

}  // namespace

bool PerfParser::FillInDsoBuildIds() {
  std::map<string, string> filenames_to_build_ids;
  reader_->GetFilenamesToBuildIDs(&filenames_to_build_ids);

  std::map<string, string> new_buildids;

  for (std::pair<const string, DSOInfo>& kv : name_to_dso_) {
    DSOInfo& dso_info = kv.second;
    const auto it = filenames_to_build_ids.find(dso_info.name);
    if (it != filenames_to_build_ids.end()) {
      dso_info.build_id = it->second;
    }
    // If there is both an existing build ID and a new build ID returned by
    // FindDsoBuildId(), overwrite the existing build ID.
    if (options_.read_missing_buildids && dso_info.hit) {
      string buildid_bin = FindDsoBuildId(dso_info);
      if (!buildid_bin.empty()) {
        dso_info.build_id = RawDataToHexString(buildid_bin);
        new_buildids[dso_info.name] = dso_info.build_id;
      }
    }
  }

  if (new_buildids.empty())
    return true;
  return reader_->InjectBuildIDs(new_buildids);
}

void PerfParser::MaybeSortParsedEvents() {
  if (!options_.sort_events_by_time)
    return;

  bool have_sample_time = true;
  for (const auto& attr : reader_->attrs()) {
    if (!(attr.attr().sample_type() & PERF_SAMPLE_TIME)) {
      have_sample_time = false;
    }
  }
  if (!have_sample_time) {
    return;
  }

  // Sort the events based on timestamp.
  std::stable_sort(parsed_events_.begin(), parsed_events_.end(),
                   CompareParsedEventTimes);

  UpdatePerfEventsFromParsedEvents();
}

void PerfParser::UpdatePerfEventsFromParsedEvents() {
  // Reorder the events in |reader_| to match the order of |parsed_events_|.
  // The |event_ptr|'s in |parsed_events_| are pointers to existing events in
  // |reader_|.
  RepeatedPtrField<PerfEvent> new_events;
  new_events.Reserve(parsed_events_.size());
  for (ParsedEvent& parsed_event : parsed_events_) {
    PerfEvent* new_event = new_events.Add();
    new_event->Swap(parsed_event.event_ptr);
    parsed_event.event_ptr = new_event;
  }

  reader_->mutable_events()->Swap(&new_events);
}

bool PerfParser::MapSampleEvent(ParsedEvent* parsed_event) {
  bool mapping_failed = false;

  const PerfEvent& event = *parsed_event->event_ptr;
  if (!event.has_sample_event() ||
      !(event.sample_event().has_ip() &&
        event.sample_event().has_pid() &&
        event.sample_event().has_tid())) {
    return false;
  }
  SampleEvent& sample_info = *parsed_event->event_ptr->mutable_sample_event();

  // Find the associated command.
  PidTid pidtid = std::make_pair(sample_info.pid(), sample_info.tid());
  const auto comm_iter = pidtid_to_comm_map_.find(pidtid);
  if (comm_iter != pidtid_to_comm_map_.end())
    parsed_event->set_command(comm_iter->second);

  const uint64_t unmapped_event_ip = sample_info.ip();
  uint64_t remapped_event_ip = 0;

  // Map the event IP itself.
  if (!MapIPAndPidAndGetNameAndOffset(sample_info.ip(),
                                      pidtid,
                                      &remapped_event_ip,
                                      &parsed_event->dso_and_offset)) {
    mapping_failed = true;
  } else {
    sample_info.set_ip(remapped_event_ip);
  }

  if (sample_info.callchain_size() &&
      !MapCallchain(sample_info.ip(),
                    pidtid,
                    unmapped_event_ip,
                    sample_info.mutable_callchain(),
                    parsed_event)) {
    mapping_failed = true;
  }

  if (sample_info.branch_stack_size() &&
      !MapBranchStack(pidtid,
                      sample_info.mutable_branch_stack(),
                      parsed_event)) {
    mapping_failed = true;
  }

  return !mapping_failed;
}

bool PerfParser::MapCallchain(const uint64_t ip,
                              const PidTid pidtid,
                              const uint64_t original_event_addr,
                              RepeatedField<uint64>* callchain,
                              ParsedEvent* parsed_event) {
  if (!callchain) {
    LOG(ERROR) << "NULL call stack data.";
    return false;
  }

  bool mapping_failed = false;

  // If the callchain's length is 0, there is no work to do.
  if (callchain->size() == 0)
    return true;

  // Keeps track of whether the current entry is kernel or user.
  parsed_event->callchain.resize(callchain->size());
  int num_entries_mapped = 0;
  for (int i = 0; i < callchain->size(); ++i) {
    uint64_t entry = callchain->Get(i);
    // When a callchain context entry is found, do not attempt to symbolize it.
    if (entry >= PERF_CONTEXT_MAX) {
      continue;
    }
    // The sample address has already been mapped so no need to map it.
    if (entry == original_event_addr) {
      callchain->Set(i, ip);
      continue;
    }
    uint64_t mapped_addr = 0;
    if (!MapIPAndPidAndGetNameAndOffset(
            entry,
            pidtid,
            &mapped_addr,
            &parsed_event->callchain[num_entries_mapped++])) {
      mapping_failed = true;
    } else {
      callchain->Set(i, mapped_addr);
    }
  }
  // Not all the entries were mapped.  Trim |parsed_event->callchain| to
  // remove unused entries at the end.
  parsed_event->callchain.resize(num_entries_mapped);

  return !mapping_failed;
}

bool PerfParser::MapBranchStack(
    const PidTid pidtid,
    RepeatedPtrField<BranchStackEntry>* branch_stack,
    ParsedEvent* parsed_event) {
  if (!branch_stack) {
    LOG(ERROR) << "NULL branch stack data.";
    return false;
  }

  // First, trim the branch stack to remove trailing null entries.
  size_t trimmed_size = 0;
  for (const BranchStackEntry& entry : *branch_stack) {
    // Count the number of non-null entries before the first null entry.
    if (IsNullBranchStackEntry(entry))
      break;
    ++trimmed_size;
  }

  // If a null entry was found, make sure all subsequent null entries are NULL
  // as well.
  for (int i = trimmed_size; i < branch_stack->size(); ++i) {
    const BranchStackEntry& entry = branch_stack->Get(i);
    if (!IsNullBranchStackEntry(entry)) {
      LOG(ERROR) << "Non-null branch stack entry found after null entry: "
                 << reinterpret_cast<void*>(entry.from_ip()) << " -> "
                 << reinterpret_cast<void*>(entry.to_ip());
      return false;
    }
  }

  // Map branch stack addresses.
  parsed_event->branch_stack.resize(trimmed_size);
  for (unsigned int i = 0; i < trimmed_size; ++i) {
    BranchStackEntry* entry = branch_stack->Mutable(i);
    ParsedEvent::BranchEntry& parsed_entry = parsed_event->branch_stack[i];

    uint64_t from_mapped = 0;
    if (!MapIPAndPidAndGetNameAndOffset(entry->from_ip(),
                                        pidtid,
                                        &from_mapped,
                                        &parsed_entry.from)) {
      return false;
    }
    entry->set_from_ip(from_mapped);

    uint64_t to_mapped = 0;
    if (!MapIPAndPidAndGetNameAndOffset(entry->to_ip(),
                                        pidtid,
                                        &to_mapped,
                                        &parsed_entry.to)) {
      return false;
    }
    entry->set_to_ip(to_mapped);

    parsed_entry.predicted = !entry->mispredicted();
  }

  return true;
}

bool PerfParser::MapIPAndPidAndGetNameAndOffset(
    uint64_t ip,
    PidTid pidtid,
    uint64_t* new_ip,
    ParsedEvent::DSOAndOffset* dso_and_offset) {
  DCHECK(dso_and_offset);
  // Attempt to find the synthetic address of the IP sample in this order:
  // 1. Address space of the kernel.
  // 2. Address space of its own process.
  // 3. Address space of the parent process.

  uint64_t mapped_addr = 0;

  // Sometimes the first event we see is a SAMPLE event and we don't have the
  // time to create an address mapper for a process. Example, for pid 0.
  AddressMapper* mapper = GetOrCreateProcessMapper(pidtid.first).first;
  bool mapped = mapper->GetMappedAddress(ip, &mapped_addr);

  if (mapped) {
    uint64_t id = UINT64_MAX;
    CHECK(mapper->GetMappedIDAndOffset(ip, &id, &dso_and_offset->offset_));
    // Make sure the ID points to a valid event.
    CHECK_LE(id, parsed_events_.size());
    ParsedEvent& parsed_event = parsed_events_[id];
    const auto& event = parsed_event.event_ptr;
    DCHECK(event->has_mmap_event()) << "Expected MMAP or MMAP2 event";

    // Find the mmap DSO filename in the set of known DSO names.
    auto dso_iter = name_to_dso_.find(event->mmap_event().filename());
    CHECK(dso_iter != name_to_dso_.end());
    dso_and_offset->dso_info_ = &dso_iter->second;

    dso_iter->second.hit = true;
    dso_iter->second.threads.insert(pidtid);
    ++parsed_event.num_samples_in_mmap_region;

    if (options_.do_remap) {
      if (GetPageAlignedOffset(mapped_addr) != GetPageAlignedOffset(ip)) {
        LOG(ERROR) << "Remapped address " << std::hex << mapped_addr << " "
                   << "does not have the same page alignment offset as "
                   << "original address " << ip;
        return false;
      }
      *new_ip = mapped_addr;
    } else {
      *new_ip = ip;
    }
  }
  return mapped;
}

bool PerfParser::MapMmapEvent(PerfDataProto_MMapEvent* event, uint64_t id) {
  // We need to hide only the real kernel addresses.  However, to make things
  // more secure, and make the mapping idempotent, we should remap all
  // addresses, both kernel and non-kernel.

  AddressMapper* mapper = GetOrCreateProcessMapper(event->pid()).first;

  uint64_t start = event->start();
  uint64_t len = event->len();
  uint64_t pgoff = event->pgoff();

  // |id| == 0 corresponds to the kernel mmap. We have several cases here:
  //
  // For ARM and x86, in sudo mode, pgoff == start, example:
  // start=0x80008200
  // pgoff=0x80008200
  // len  =0xfffffff7ff7dff
  //
  // For x86-64, in sudo mode, pgoff is between start and start + len. SAMPLE
  // events lie between pgoff and pgoff + length of the real kernel binary,
  // example:
  // start=0x3bc00000
  // pgoff=0xffffffffbcc00198
  // len  =0xffffffff843fffff
  // SAMPLE events will be found after pgoff. For kernels with ASLR, pgoff will
  // be something only visible to the root user, and will be randomized at
  // startup. With |remap| set to true, we should hide pgoff in this case. So we
  // normalize all SAMPLE events relative to pgoff.
  //
  // For non-sudo mode, the kernel will be mapped from 0 to the pointer limit,
  // example:
  // start=0x0
  // pgoff=0x0
  // len  =0xffffffff
  if (id == 0) {
    // If pgoff is between start and len, we normalize the event by setting
    // start to be pgoff just like how it is for ARM and x86. We also set len to
    // be a much smaller number (closer to the real length of the kernel binary)
    // because SAMPLEs are actually only seen between |event->pgoff| and
    // |event->pgoff + kernel text size|.
    if (pgoff > start && pgoff < start + len) {
      len = len + start - pgoff;
      start = pgoff;
    }
    // For kernels with ALSR pgoff is critical information that should not be
    // revealed when |remap| is true.
    pgoff = 0;
  }

  if (!mapper->MapWithID(start, len, id, pgoff, true)) {
    mapper->DumpToLog();
    return false;
  }

  if (options_.do_remap) {
    uint64_t mapped_addr;
    if (!mapper->GetMappedAddress(start, &mapped_addr)) {
      LOG(ERROR) << "Failed to map starting address " << std::hex << start;
      return false;
    }
    if (GetPageAlignedOffset(mapped_addr) != GetPageAlignedOffset(start)) {
      LOG(ERROR) << "Remapped address " << std::hex << mapped_addr << " "
                 << "does not have the same page alignment offset as start "
                 << "address " << start;
      return false;
    }

    event->set_start(mapped_addr);
    event->set_len(len);
    event->set_pgoff(pgoff);
  }
  return true;
}

bool PerfParser::MapCommEvent(const PerfDataProto_CommEvent& event) {
  GetOrCreateProcessMapper(event.pid());
  return true;
}

bool PerfParser::MapForkEvent(const PerfDataProto_ForkEvent& event) {
  PidTid parent = std::make_pair(event.ppid(), event.ptid());
  PidTid child = std::make_pair(event.pid(), event.tid());
  if (parent != child) {
    auto parent_iter = pidtid_to_comm_map_.find(parent);
    if (parent_iter != pidtid_to_comm_map_.end())
      pidtid_to_comm_map_[child] = parent_iter->second;
  }

  const uint32_t pid = event.pid();

  // If the parent and child pids are the same, this is just a new thread
  // within the same process, so don't do anything.
  if (event.ppid() == pid)
    return true;

  if (!GetOrCreateProcessMapper(pid, event.ppid()).second) {
    DVLOG(1) << "Found an existing process mapper with pid: " << pid;
  }

  return true;
}

std::pair<AddressMapper*, bool> PerfParser::GetOrCreateProcessMapper(
    uint32_t pid, uint32_t ppid) {
  const auto& search = process_mappers_.find(pid);
  if (search != process_mappers_.end()) {
    return std::make_pair(search->second.get(), false);
  }

  std::unique_ptr<AddressMapper> mapper;
  const auto& parent_mapper = process_mappers_.find(ppid);
  if (parent_mapper != process_mappers_.end()) {
    mapper.reset(new AddressMapper(*parent_mapper->second));
  } else {
    mapper.reset(new AddressMapper());
    mapper->set_page_alignment(kMmapPageAlignment);
  }

  const auto inserted =
      process_mappers_.insert(search, std::make_pair(pid, std::move(mapper)));
  return std::make_pair(inserted->second.get(), true);
}

}  // namespace quipper
