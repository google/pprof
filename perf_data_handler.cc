/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "int_compat.h"
#include "intervalmap.h"
#include "path_matching.h"
#include "perf_data_handler.h"
#include "string_compat.h"
#include "quipper/perf_reader.h"

using quipper::PerfDataProto;
using quipper::PerfDataProto_MMapEvent;
using quipper::PerfDataProto_CommEvent;

namespace perftools {
namespace {

// Normalizer processes a PerfDataProto and maintains tables to the
// current metadata for each process.  It drives callbacks to
// PerfDataHandler with samples in a fully normalized form.
class Normalizer {
 public:
  Normalizer(const PerfDataProto& perf_proto, PerfDataHandler* handler)
      : perf_proto_(perf_proto), handler_(handler) {
    for (const auto& build_id : perf_proto_.build_ids()) {
      const string& bytes = build_id.build_id_hash();
      std::stringstream hex;
      for (size_t i = 0; i < bytes.size(); ++i) {
        // The char must be turned into an int to be used by stringstream;
        // however, if the byte's value -8 it should be turned to 0x00f8 as an
        // int, not 0xfff8. This cast solves this problem.
        const auto& byte = static_cast<unsigned char>(bytes[i]);
        hex << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(byte);
      }
      if (build_id.filename() != "") {
        filename_to_build_id_[build_id.filename()] = hex.str();
      } else {
        std::stringstream filename;
        filename << std::hex << build_id.filename_md5_prefix();
        filename_to_build_id_[filename.str().c_str()] = hex.str();
      }
    }

    uint64 current_event_index = 0;
    for (const auto& attr : perf_proto_.file_attrs()) {
      for (uint64 id : attr.ids()) {
        id_to_event_index_[id] = current_event_index;
      }
      current_event_index++;
    }
  }

  Normalizer(const Normalizer&) = delete;
  Normalizer& operator=(const Normalizer&) = delete;

  ~Normalizer() {}

  // Convert to a protobuf using quipper and then aggregate the results.
  void Normalize();

 private:
  // Using a 32-bit type for the PID values as the max PID value on 64-bit
  // systems is 2^22, see http://man7.org/linux/man-pages/man5/proc.5.html.
  typedef std::unordered_map<uint32, PerfDataHandler::Mapping*> PidToMMapMap;
  typedef std::unordered_map<uint32, const PerfDataProto_CommEvent*>
      PidToCommMap;

  typedef IntervalMap<const PerfDataHandler::Mapping*> MMapIntervalMap;

  // Copy the parent's mmaps/comm if they exist.  Otherwise, items
  // will be lazily populated.
  void UpdateMapsWithMMapEvent(const quipper::PerfDataProto_MMapEvent* mmap);

  void UpdateMapsWithForkEvent(const quipper::PerfDataProto_ForkEvent& fork);
  void LogStats();

  // Normalize the sample_event in event_proto and call handler_->Sample
  void InvokeHandleSample(const quipper::PerfDataProto::PerfEvent& perf_event);

  // Find the MMAP event which has ip in its address range from pid.  If no
  // mapping is found, returns nullptr.
  const PerfDataHandler::Mapping* TryLookupInPid(uint32 pid, uint64 ip) const;

  // Find the mapping for a given ip given a pid context (in user or kernel
  // mappings); returns nullptr if none can be found.
  const PerfDataHandler::Mapping* GetMappingFromPidAndIP(uint32 pid,
                                                         uint64 ip) const;

  // Find the main MMAP event for this pid.  If no mapping is found,
  // nullptr is returned.
  const PerfDataHandler::Mapping* GetMainMMapFromPid(uint32 pid) const;

  // For profiles with a single event, perf doesn't bother sending the
  // id.  So, if there is only one event, the event index must be 0.
  // Returns the event index corresponding to the id for this sample, or
  // -1 for an error.
  int64 GetEventIndexForSample(
      const quipper::PerfDataProto_SampleEvent& sample) const;

  const quipper::PerfDataProto& perf_proto_;
  PerfDataHandler* handler_;  // unowned.

  // Mapping we have allocated.
  std::vector<std::unique_ptr<PerfDataHandler::Mapping>> owned_mappings_;
  std::vector<std::unique_ptr<quipper::PerfDataProto_MMapEvent>>
      owned_quipper_mappings_;

  // The event for a given sample is determined by the id.
  // Map each id to an index in the event_profiles_ vector.
  std::unordered_map<uint64, uint64> id_to_event_index_;

  // pid_to_comm_event maps a pid to the corresponding comm event.
  PidToCommMap pid_to_comm_event_;

  // pid_to_mmaps maps a pid to all mmap events that correspond to that pid.
  std::unordered_map<uint32, std::unique_ptr<MMapIntervalMap>> pid_to_mmaps_;

  // pid_to_executable_mmap maps a pid to mmap that most likely contains the
  // filename of the main executable for that pid.
  PidToMMapMap pid_to_executable_mmap_;

  // map filenames to build-ids.
  std::unordered_map<string, string> filename_to_build_id_;

  struct {
    int64 samples = 0;
    int64 missing_main_mmap = 0;
    int64 missing_sample_mmap = 0;

    int64 callchain_ips = 0;
    int64 missing_callchain_mmap = 0;

    int64 branch_stack_ips = 0;
    int64 missing_branch_stack_mmap = 0;

    int64 no_event_errors = 0;
  } stat_;
};

void Normalizer::UpdateMapsWithForkEvent(
    const quipper::PerfDataProto_ForkEvent& fork) {
  if (fork.pid() == fork.ppid()) {
    // Don't care about threads.
    return;
  }
  const auto& it = pid_to_mmaps_.find(fork.ppid());
  if (it != pid_to_mmaps_.end()) {
    pid_to_mmaps_[fork.pid()] = std::unique_ptr<MMapIntervalMap>(
        new MMapIntervalMap(*it->second.get()));
  }
  auto comm_it = pid_to_comm_event_.find(fork.ppid());
  if (comm_it != pid_to_comm_event_.end()) {
    pid_to_comm_event_[fork.pid()] = comm_it->second;
  }
  auto exec_mmap_it = pid_to_executable_mmap_.find(fork.ppid());
  if (exec_mmap_it != pid_to_executable_mmap_.end()) {
    pid_to_executable_mmap_[fork.pid()] = exec_mmap_it->second;
  }
}

inline bool HasPrefixString(const string& haystack, const char* needle) {
  const size_t needle_len = strlen(needle);
  const size_t haystack_len = haystack.length();
  return haystack_len >= needle_len &&
         haystack.compare(0, needle_len, needle) == 0;
}

inline bool HasSuffixString(const string& haystack, const char* needle) {
  const size_t needle_len = strlen(needle);
  const size_t haystack_len = haystack.length();
  return haystack_len >= needle_len &&
         haystack.compare(haystack_len - needle_len, needle_len, needle) == 0;
}

void Normalizer::Normalize() {
  for (const auto& event_proto : perf_proto_.events()) {
    if (event_proto.has_mmap_event()) {
      UpdateMapsWithMMapEvent(&event_proto.mmap_event());
    } else if (event_proto.has_comm_event()) {
      if (event_proto.comm_event().pid() == event_proto.comm_event().tid()) {
        // pid==tid happens on exec()
        pid_to_executable_mmap_.erase(event_proto.comm_event().pid());
        pid_to_comm_event_[event_proto.comm_event().pid()] =
            &event_proto.comm_event();
      }
      PerfDataHandler::CommContext comm_context;
      comm_context.comm = &event_proto.comm_event();
      handler_->Comm(comm_context);
    } else if (event_proto.has_fork_event()) {
      UpdateMapsWithForkEvent(event_proto.fork_event());
    } else if (event_proto.has_lost_event()) {
      stat_.samples += event_proto.lost_event().lost();
      stat_.missing_main_mmap += event_proto.lost_event().lost();
      stat_.missing_sample_mmap += event_proto.lost_event().lost();
      quipper::PerfDataProto::SampleEvent sample;
      quipper::PerfDataProto::EventHeader header;
      sample.set_id(event_proto.lost_event().id());
      sample.set_pid(event_proto.lost_event().sample_info().pid());
      sample.set_tid(event_proto.lost_event().sample_info().tid());
      PerfDataHandler::SampleContext context(header, sample);
      context.file_attrs_index = GetEventIndexForSample(sample);
      if (context.file_attrs_index == -1) {
        ++stat_.no_event_errors;
        continue;
      }
      for (uint64 i = 0; i < event_proto.lost_event().lost(); ++i) {
        handler_->Sample(context);
      }
    } else if (event_proto.has_sample_event()) {
      InvokeHandleSample(event_proto);
    }
  }

  LogStats();
}

void Normalizer::InvokeHandleSample(
    const quipper::PerfDataProto::PerfEvent& event_proto) {
  if (!event_proto.has_sample_event()) {
    std::cerr << "Expected sample event." << std::endl;
    abort();
  }
  const auto& sample = event_proto.sample_event();
  PerfDataHandler::SampleContext context(event_proto.header(),
                                         event_proto.sample_event());
  context.file_attrs_index = GetEventIndexForSample(context.sample);
  if (context.file_attrs_index == -1) {
    ++stat_.no_event_errors;
    return;
  }
  ++stat_.samples;

  uint32 pid = sample.pid();

  context.sample_mapping = GetMappingFromPidAndIP(pid, sample.ip());
  stat_.missing_sample_mmap += context.sample_mapping == nullptr;

  context.main_mapping = GetMainMMapFromPid(pid);
  std::unique_ptr<PerfDataHandler::Mapping> fake;
  // Kernel samples might take some extra work.
  if (context.main_mapping == nullptr &&
      (event_proto.header().misc() & PERF_RECORD_MISC_CPUMODE_MASK) ==
          PERF_RECORD_MISC_KERNEL) {
    auto comm_it = pid_to_comm_event_.find(pid);
    auto kernel_it = pid_to_executable_mmap_.find(-1);
    if (comm_it != pid_to_comm_event_.end()) {
      const string* build_id = nullptr;
      if (kernel_it != pid_to_executable_mmap_.end()) {
        build_id = kernel_it->second->build_id;
      }
      fake.reset(new PerfDataHandler::Mapping(&comm_it->second->comm(),
                                              build_id, 0, 1, 0, 0));
      context.main_mapping = fake.get();
    } else if (pid == 0 && kernel_it != pid_to_executable_mmap_.end()) {
      context.main_mapping = kernel_it->second;
    }
  }

  stat_.missing_main_mmap += context.main_mapping == nullptr;

  // Normalize the callchain.
  context.callchain.resize(sample.callchain_size());
  for (int i = 0; i < sample.callchain_size(); ++i) {
    ++stat_.callchain_ips;
    context.callchain[i].ip = sample.callchain(i);
    context.callchain[i].mapping =
        GetMappingFromPidAndIP(pid, sample.callchain(i));
    stat_.missing_callchain_mmap += context.callchain[i].mapping == nullptr;
  }

  // Normalize the branch_stack.
  context.branch_stack.resize(sample.branch_stack_size());
  for (int i = 0; i < sample.branch_stack_size(); ++i) {
    stat_.branch_stack_ips += 2;
    auto bse = sample.branch_stack(i);
    // from
    context.branch_stack[i].from.ip = bse.from_ip();
    context.branch_stack[i].from.mapping =
        GetMappingFromPidAndIP(pid, bse.from_ip());
    stat_.missing_branch_stack_mmap +=
        context.branch_stack[i].from.mapping == nullptr;
    // to
    context.branch_stack[i].to.ip = bse.to_ip();
    context.branch_stack[i].to.mapping =
        GetMappingFromPidAndIP(pid, bse.to_ip());
    stat_.missing_branch_stack_mmap +=
        context.branch_stack[i].to.mapping == nullptr;
    // mispredicted
    context.branch_stack[i].mispredicted = bse.mispredicted();
  }

  handler_->Sample(context);
}

static void CheckStat(int64 num, int64 denom, const string& desc) {
  const int max_missing_pct = 1;
  if (denom > 0 && num * 100 / denom > max_missing_pct) {
    LOG(ERROR) << "stat: " << desc << " " << num << "/" << denom;
  }
}

void Normalizer::LogStats() {
  CheckStat(stat_.missing_main_mmap, stat_.samples, "missing_main_mmap");
  CheckStat(stat_.missing_sample_mmap, stat_.samples, "missing_sample_mmap");
  CheckStat(stat_.missing_callchain_mmap, stat_.callchain_ips,
            "missing_callchain_mmap");
  CheckStat(stat_.missing_branch_stack_mmap, stat_.branch_stack_ips,
            "missing_branch_stack_mmap");
  CheckStat(stat_.no_event_errors, 1, "unknown event id");
}

static bool IsVirtualMapping(const string& map_name) {
  return HasPrefixString(map_name, "//") ||
         (HasPrefixString(map_name, "[") && HasSuffixString(map_name, "]"));
}

void Normalizer::UpdateMapsWithMMapEvent(
    const quipper::PerfDataProto_MMapEvent* mmap) {
  if (mmap->len() == 0) {
    LOG(WARNING) << "bogus mapping: " << mmap->filename();
    return;
  }
  uint32 pid = mmap->pid();
  MMapIntervalMap* interval_map = nullptr;
  const auto& it = pid_to_mmaps_.find(pid);
  if (it == pid_to_mmaps_.end()) {
    interval_map = new MMapIntervalMap;
    pid_to_mmaps_[pid] = std::unique_ptr<MMapIntervalMap>(interval_map);
  } else {
    interval_map = it->second.get();
  }
  std::unordered_map<string, string>::const_iterator build_id_it;
  if (mmap->filename() != "") {
    build_id_it = filename_to_build_id_.find(mmap->filename());
  } else {
    std::stringstream filename;
    filename << std::hex << mmap->filename_md5_prefix();
    build_id_it = filename_to_build_id_.find(filename.str());
  }

  const string* build_id = build_id_it == filename_to_build_id_.end()
                               ? nullptr
                               : &build_id_it->second;
  PerfDataHandler::Mapping* mapping = new PerfDataHandler::Mapping(
      &mmap->filename(), build_id, mmap->start(), mmap->start() + mmap->len(),
      mmap->pgoff(), mmap->filename_md5_prefix());
  owned_mappings_.emplace_back(mapping);
  if (mapping->file_offset > (static_cast<uint64>(1) << 63) &&
      mapping->limit > (static_cast<uint64>(1) << 63)) {
    // kernel is funky and basically swaps start and offset.  Arrange
    // them such that we can reasonably symbolize them later.
    uint64 old_start = mapping->start;
    // file_offset here actually refers to the address of the _stext
    // kernel symbol, so we need to align it.
    mapping->start = mapping->file_offset - mapping->file_offset % 4096;
    mapping->file_offset = old_start;
  }

  interval_map->Set(mapping->start, mapping->limit, mapping);
  // Pass the final mapping through to the subclass also.
  PerfDataHandler::MMapContext mmap_context;
  mmap_context.pid = pid;
  mmap_context.mapping = mapping;
  handler_->MMap(mmap_context);

  // Main executables are usually loaded at 0x8048000 or 0x400000.
  // If we ever see an MMAP starting at one of those locations, that should be
  // our guess.
  // This is true even if the old MMAP started at one of the locations, because
  // the pid may have been recycled since then (so newer is better).
  if (mapping->start == 0x8048000 || mapping->start == 0x400000) {
    pid_to_executable_mmap_[pid] = mapping;
    return;
  }
  // Figure out whether this MMAP is the main executable.
  // If there have been no previous MMAPs for this pid, then this MMAP is our
  // best guess.
  auto old_mapping_it = pid_to_executable_mmap_.find(pid);
  PerfDataHandler::Mapping* old_mapping =
      old_mapping_it == pid_to_executable_mmap_.end() ? nullptr
                                                      : old_mapping_it->second;

  if (old_mapping != nullptr && old_mapping->start == 0x400000 &&
      (old_mapping->filename == nullptr || *old_mapping->filename == "") &&
      mapping->start - mapping->file_offset == 0x400000) {
    // Hugepages remap the main binary, but the original mapping loses
    // its name, so we have this hack.
    old_mapping->filename = &mmap->filename();
  }

  static const char kKernelPrefix[] = "[kernel.kallsyms]";

  if (old_mapping == nullptr && !HasSuffixString(mmap->filename(), ".ko") &&
      !HasSuffixString(mmap->filename(), ".so") &&
      !IsDeletedSharedObject(mmap->filename()) &&
      !IsVersionedSharedObject(mmap->filename()) &&
      !IsVirtualMapping(mmap->filename()) &&
      !HasPrefixString(mmap->filename(), kKernelPrefix)) {
    if (!HasPrefixString(mmap->filename(), "/usr/bin") &&
        !HasPrefixString(mmap->filename(), "/usr/sbin") &&
        !HasSuffixString(mmap->filename(), "/sel_ldr")) {
      LOG(INFO) << "guessing main for pid: " << pid << " " << mmap->filename();
    }
    pid_to_executable_mmap_[pid] = mapping;
    return;
  }

  if (pid == std::numeric_limits<uint32>::max() &&
      HasPrefixString(mmap->filename(), kKernelPrefix)) {
    pid_to_executable_mmap_[pid] = mapping;
  }
}

const PerfDataHandler::Mapping* Normalizer::TryLookupInPid(uint32 pid,
                                                           uint64 ip) const {
  const auto& it = pid_to_mmaps_.find(pid);
  if (it == pid_to_mmaps_.end()) {
    VLOG(2) << "No mmaps for pid " << pid;
    return nullptr;
  }
  MMapIntervalMap* mmaps = it->second.get();

  const PerfDataHandler::Mapping* mapping = nullptr;
  mmaps->Lookup(ip, &mapping);
  return mapping;
}

// Find the mapping for ip in the context of pid.  We might be looking
// at a kernel IP, however (which can show up in any pid, and are
// stored in our map as pid = -1), so check there if the lookup fails
// in our process.
const PerfDataHandler::Mapping* Normalizer::GetMappingFromPidAndIP(
    uint32 pid, uint64 ip) const {
  if (ip >= PERF_CONTEXT_MAX) {
    // These aren't real IPs, they're context hints.  Drop them.
    return nullptr;
  }
  // One could try to decide if this is a kernel or user sample
  // directly.  ahh@ thinks there's a heuristic that should work on
  // x86 (basically without any error): all kernel samples should have
  // 16 high bits set, all user samples should have high 16 bits
  // cleared.  But that's not portable, and on any arch (...hopefully)
  // the user/kernel mappings should be disjoint anyway, so just check
  // both, starting with user.  We could also use PERF_CONTEXT_KERNEL
  // and friends (see for instance how perf handles this:
  // https://goto.google.com/udgor) to know whether to check user or
  // kernel, but this seems more robust.
  const PerfDataHandler::Mapping* mapping = TryLookupInPid(pid, ip);
  if (mapping == nullptr) {
    // Might be a kernel sample.
    mapping = TryLookupInPid(-1, ip);
  }
  if (mapping == nullptr) {
    VLOG(2) << "no sample mmap found for pid " << pid << " and ip " << ip;
    return nullptr;
  }
  if (ip < mapping->start || ip >= mapping->limit) {
    std::cerr << "IP is not in mapping." << std::endl
              << "IP: " << ip << std::endl
              << "Start: " << mapping->start << std::endl
              << "Limit: " << mapping->limit << std::endl;
    abort();
  }
  return mapping;
}

const PerfDataHandler::Mapping* Normalizer::GetMainMMapFromPid(
    uint32 pid) const {
  auto mapping_it = pid_to_executable_mmap_.find(pid);
  if (mapping_it != pid_to_executable_mmap_.end()) {
    return mapping_it->second;
  }

  VLOG(2) << "No argv0 name found for sample with pid: " << pid;
  return nullptr;
}

int64 Normalizer::GetEventIndexForSample(
    const quipper::PerfDataProto_SampleEvent& sample) const {
  if (perf_proto_.file_attrs().size() == 1) {
    return 0;
  }

  if (!sample.has_id()) {
    LOG(ERROR) << "Perf sample did not have id";
    return -1;
  }

  auto it = id_to_event_index_.find(sample.id());
  if (it == id_to_event_index_.end()) {
    LOG(ERROR) << "Incorrect event id: " << sample.id();
    return -1;
  }
  return it->second;
}
}  // namespace

// Finds needle in haystack starting at cursor. It then returns the index
// directly after needle or string::npos if needle was not found.
size_t FindAfter(const string& haystack, const string& needle, size_t cursor) {
  auto next_cursor = haystack.find(needle, cursor);
  if (next_cursor != string::npos) {
    next_cursor += needle.size();
  }
  return next_cursor;
}

bool IsDeletedSharedObject(const string& path) {
  size_t cursor = 1;
  while ((cursor = FindAfter(path, ".so", cursor)) != string::npos) {
    const auto ch = path.at(cursor);
    if (ch == '.' || ch == '_' || ch == ' ') {
      return path.find("(deleted)", cursor) != string::npos;
    }
  }
  return false;
}

bool IsVersionedSharedObject(const string& path) {
  return path.find(".so.", 1) != string::npos;
}

PerfDataHandler::PerfDataHandler() {}

void PerfDataHandler::Process(const quipper::PerfDataProto& perf_proto,
                              PerfDataHandler* handler) {
  Normalizer Normalizer(perf_proto, handler);
  return Normalizer.Normalize();
}

}  // namespace perftools
