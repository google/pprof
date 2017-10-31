/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "perf_data_converter.h"

#include <algorithm>
#include <deque>
#include <map>
#include <sstream>
#include <vector>

#include "int_compat.h"
#include "perf_data_handler.h"
#include "string_compat.h"
#include "builder.h"
#include "quipper/perf_data.pb.h"
#include "quipper/perf_parser.h"
#include "quipper/perf_reader.h"

namespace perftools {
namespace {

typedef perftools::profiles::Profile Profile;
typedef perftools::profiles::Builder ProfileBuilder;

typedef uint32 Pid;

enum ExecutionMode {
  Unknown,
  HostKernel,
  HostUser,
  GuestKernel,
  GuestUser,
  Hypervisor
};

const char* ExecModeString(ExecutionMode mode) {
  switch (mode) {
    case HostKernel:
      return ExecutionModeHostKernel;
    case HostUser:
      return ExecutionModeHostUser;
    case GuestKernel:
      return ExecutionModeGuestKernel;
    case GuestUser:
      return ExecutionModeGuestUser;
    case Hypervisor:
      return ExecutionModeHypervisor;
    default:
      std::cerr << "Execution mode not handled: " << mode << std::endl;
      return "";
  }
}

ExecutionMode PerfExecMode(const PerfDataHandler::SampleContext& sample) {
  if (sample.header.has_misc()) {
    switch (sample.header.misc() & PERF_RECORD_MISC_CPUMODE_MASK) {
      case PERF_RECORD_MISC_KERNEL:
        return HostKernel;
      case PERF_RECORD_MISC_USER:
        return HostUser;
      case PERF_RECORD_MISC_GUEST_KERNEL:
        return GuestKernel;
      case PERF_RECORD_MISC_GUEST_USER:
        return GuestUser;
      case PERF_RECORD_MISC_HYPERVISOR:
        return Hypervisor;
    }
  }
  return Unknown;
}

// Adds the string to the profile builder. If the UTF-8 library is included,
// this also ensures the string contains structurally valid UTF-8.
// In order to successfully unmarshal the proto in Go, all strings inserted into
// the profile string table must be valid UTF-8.
int64 UTF8StringId(const string& s, ProfileBuilder* builder) {
  return builder->StringId(s.c_str());
}

// Returns the file name of the mapping as either the real file path if it's
// present or the string representation of the file path MD5 checksum prefix
// when the real file path was stripped from the data for privacy reasons.
string MappingFilename(const PerfDataHandler::Mapping* m) {
  if (m->filename != nullptr && !m->filename->empty()) {
    return *m->filename;
  } else if (m->filename_md5_prefix != 0) {
    std::stringstream filename;
    filename << std::hex << m->filename_md5_prefix;
    return filename.str();
  }
  return "";
}

// List of profile location IDs, currently used to represent a call stack.
typedef std::vector<uint64> LocationIdVector;

// It is sufficient to key the location and mapping maps by PID.
// However, when Samples include labels, it is necessary to key their maps
// not only by PID, but also by anything their labels may contain, since labels
// are also distinguishing features.  This struct should contain everything
// required to uniquely identify a Sample: if two Samples you consider different
// end up with the same SampleKey, you should extend SampleKey til they don't.
//
// If any of these values are not used as labels, they should be set to 0.
struct SampleKey {
  Pid pid = 0;
  Pid tid = 0;
  uint64 time_ns = 0;
  ExecutionMode exec_mode = Unknown;
  // The index of the sample's command in the profile's string table.
  uint64 comm = 0;
  LocationIdVector stack;
};

struct SampleKeyEqualityTester {
  bool operator()(const SampleKey a, const SampleKey b) const {
    return ((a.pid == b.pid) && (a.tid == b.tid) && (a.time_ns == b.time_ns) &&
            (a.exec_mode == b.exec_mode) && (a.comm == b.comm) &&
            (a.stack == b.stack));
  }
};

struct SampleKeyHasher {
  size_t operator()(const SampleKey k) const {
    size_t hash = 0;
    hash ^= std::hash<int32>()(k.pid);
    hash ^= std::hash<int32>()(k.tid);
    hash ^= std::hash<uint64>()(k.time_ns);
    hash ^= std::hash<int>()(k.exec_mode);
    hash ^= std::hash<uint64>()(k.comm);
    for (const auto& id : k.stack) {
      hash ^= std::hash<uint64>()(id);
    }
    return hash;
  }
};

// While Locations and Mappings are per-address-space (=per-process), samples
// can be thread-specific.  If the requested sample labels include PID and
// TID, we'll need to maintain separate profile sample objects for samples
// that are identical except for TID.  Likewise, if the requested sample
// labels include timestamp_ns, then we'll need to have separate
// profile_proto::Samples for samples that are identical except for timestamp.
typedef std::unordered_map<SampleKey, perftools::profiles::Sample*,
                           SampleKeyHasher, SampleKeyEqualityTester> SampleMap;

// Map from a virtual address to a profile location ID. It only keys off the
// address, not also the mapping ID since the map / its portions are invalidated
// by Comm() and MMap() methods to force re-creation of those locations.
//
typedef std::map<uint64, uint64> LocationMap;

// Map from the handler mapping object to profile mapping ID. The mappings
// the handler creates are immutable and reasonably shared (as in no new mapping
// object is created per, say, each sample), so using the pointers is OK.
typedef std::unordered_map<const PerfDataHandler::Mapping*, uint64> MappingMap;

// Per-process (aggregated when no PID grouping requested) info.
// See docs on ProcessProfile in the header file for details on the fields.
class ProcessMeta {
 public:
  // Constructs the object for the specified PID.
  explicit ProcessMeta(Pid pid) : pid_(pid) {}

  // Updates the bounding time interval ranges per specified timestamp.
  void UpdateTimestamps(int64 time_nsec) {
    if (min_sample_time_ns_ == 0 || time_nsec < min_sample_time_ns_) {
      min_sample_time_ns_ = time_nsec;
    }
    if (max_sample_time_ns_ == 0 || time_nsec > max_sample_time_ns_) {
      max_sample_time_ns_ = time_nsec;
    }
  }

  std::unique_ptr<ProcessProfile> makeProcessProfile(Profile* data) {
    ProcessProfile* pp = new ProcessProfile();
    pp->pid = pid_;
    pp->data.Swap(data);
    pp->min_sample_time_ns = min_sample_time_ns_;
    pp->max_sample_time_ns = max_sample_time_ns_;
    return std::unique_ptr<ProcessProfile>(pp);
  }

 private:
  Pid pid_;
  int64 min_sample_time_ns_ = 0;
  int64 max_sample_time_ns_ = 0;
};

class PerfDataConverter : public PerfDataHandler {
 public:
  explicit PerfDataConverter(const quipper::PerfDataProto& perf_data,
                             uint32 sample_labels = kNoLabels,
                             uint32 options = kGroupByPids)
      : perf_data_(perf_data),
        sample_labels_(sample_labels),
        options_(options) {}
  PerfDataConverter(const PerfDataConverter&) = delete;
  PerfDataConverter& operator=(const PerfDataConverter&) = delete;
  virtual ~PerfDataConverter() {}

  ProcessProfiles Profiles();

  // Callbacks for PerfDataHandler
  void Sample(const PerfDataHandler::SampleContext& sample) override;
  void Comm(const CommContext& comm) override;
  void MMap(const MMapContext& mmap) override;

 private:
  // Adds a new sample updating the event counters if such sample is not present
  // in the profile initializing its metrics. Updates the metrics associated
  // with the sample if the sample was added before.
  void AddOrUpdateSample(const PerfDataHandler::SampleContext& context,
                         const Pid& pid, const SampleKey& sample_key,
                         ProfileBuilder* builder);

  // Adds a new location to the profile if such location is not present in the
  // profile, returning the ID of the location. It also adds the profile mapping
  // corresponding to the specified handler mapping.
  uint64 AddOrGetLocation(const Pid& pid, uint64 addr,
                          const PerfDataHandler::Mapping* mapping,
                          ProfileBuilder* builder);

  // Adds a new mapping to the profile if such mapping is not present in the
  // profile, returning the ID of the mapping. It returns 0 to indicate that the
  // mapping was not added (only happens if smap == 0 currently).
  uint64 AddOrGetMapping(const Pid& pid, const PerfDataHandler::Mapping* smap,
                         ProfileBuilder* builder);

  // Returns whether pid labels were requested for inclusion in the
  // profile.proto's Sample.Label field.
  bool IncludePidLabels() const { return (sample_labels_ & kPidLabel); }
  // Returns whether tid labels were requested for inclusion in the
  // profile.proto's Sample.Label field.
  bool IncludeTidLabels() const { return (sample_labels_ & kTidLabel); }
  // Returns whether timestamp_ns labels were requested for inclusion in the
  // profile.proto's Sample.Label field.
  bool IncludeTimestampNsLabels() const {
    return (sample_labels_ & kTimestampNsLabel);
  }
  // Returns whether execution_mode labels were requested for inclusion in the
  // profile.proto's Sample.Label field.
  bool IncludeExecutionModeLabels() const {
    return (sample_labels_ & kExecutionModeLabel);
  }
  // Returns whether comm labels were requested for inclusion in the
  // profile.proto's Sample.Label field.
  bool IncludeCommLabels() const { return (sample_labels_ & kCommLabel); }

  SampleKey MakeSampleKey(const PerfDataHandler::SampleContext& sample,
                          ProfileBuilder* builder);

  ProfileBuilder* GetOrCreateBuilder(
      const PerfDataHandler::SampleContext& sample);

  const quipper::PerfDataProto& perf_data_;
  // Using deque so that appends do not invalidate existing pointers.
  std::deque<ProfileBuilder> builders_;
  std::deque<ProcessMeta> process_metas_;

  struct PerPidInfo {
    ProfileBuilder* builder = nullptr;
    ProcessMeta* process_meta = nullptr;
    LocationMap location_map;
    MappingMap mapping_map;
    std::unordered_map<Pid, string> tid_to_comm_map;
    SampleMap sample_map;
    void clear() {
      builder = nullptr;
      process_meta = nullptr;
      location_map.clear();
      mapping_map.clear();
      tid_to_comm_map.clear();
      sample_map.clear();
    }
  };
  std::unordered_map<Pid, PerPidInfo> per_pid_;

  const uint32 sample_labels_;
  const uint32 options_;
};

SampleKey PerfDataConverter::MakeSampleKey(
    const PerfDataHandler::SampleContext& sample, ProfileBuilder* builder) {
  SampleKey sample_key;
  sample_key.pid = sample.sample.has_pid() ? sample.sample.pid() : 0;
  sample_key.tid =
      (IncludeTidLabels() && sample.sample.has_tid()) ? sample.sample.tid() : 0;
  sample_key.time_ns =
      (IncludeTimestampNsLabels() && sample.sample.has_sample_time_ns())
          ? sample.sample.sample_time_ns()
          : 0;
  if (IncludeExecutionModeLabels()) {
    sample_key.exec_mode = PerfExecMode(sample);
  }
  if (IncludeCommLabels() && sample.sample.has_pid() &&
      sample.sample.has_tid()) {
    Pid pid = sample.sample.pid();
    Pid tid = sample.sample.tid();
    const string& comm = per_pid_[pid].tid_to_comm_map[tid];
    if (!comm.empty()) {
      sample_key.comm = UTF8StringId(comm, builder);
    }
  }
  return sample_key;
}

ProfileBuilder* PerfDataConverter::GetOrCreateBuilder(
    const PerfDataHandler::SampleContext& sample) {
  Pid builder_pid = (options_ & kGroupByPids) ? sample.sample.pid() : 0;
  auto& per_pid = per_pid_[builder_pid];
  if (per_pid.builder == nullptr) {
    builders_.push_back(ProfileBuilder());
    per_pid.builder = &builders_.back();
    process_metas_.push_back(ProcessMeta(builder_pid));
    per_pid.process_meta = &process_metas_.back();

    ProfileBuilder* builder = per_pid.builder;
    Profile* profile = builder->mutable_profile();
    int unknown_event_idx = 0;
    for (int event_idx = 0; event_idx < perf_data_.file_attrs_size();
         ++event_idx) {
      // Come up with an event name for this event.  perf.data will usually
      // contain an event_types section of the same cardinality as its
      // file_attrs; in this case we can just use the name there.  Otherwise
      // we just give it an anonymous name.
      string event_name = "";
      if (perf_data_.file_attrs_size() == perf_data_.event_types_size()) {
        const auto& event_type = perf_data_.event_types(event_idx);
        if (event_type.has_name()) {
          event_name = event_type.name() + "_";
        }
      }
      if (event_name == "") {
        event_name = "event_" + std::to_string(unknown_event_idx++) + "_";
      }
      auto sample_type = profile->add_sample_type();
      sample_type->set_type(UTF8StringId(event_name + "sample", builder));
      sample_type->set_unit(builder->StringId("count"));
      sample_type = profile->add_sample_type();
      sample_type->set_type(UTF8StringId(event_name + "event", builder));
      sample_type->set_unit(builder->StringId("count"));
    }
    if (sample.main_mapping == nullptr) {
      auto fake_main = profile->add_mapping();
      fake_main->set_id(profile->mapping_size());
      fake_main->set_memory_start(0);
      fake_main->set_memory_limit(1);
    } else {
      AddOrGetMapping(sample.sample.pid(), sample.main_mapping, builder);
    }
    if (perf_data_.string_metadata().has_perf_version()) {
      string perf_version =
          "perf-version:" + perf_data_.string_metadata().perf_version().value();
      profile->add_comment(UTF8StringId(perf_version, builder));
    }
    if (perf_data_.string_metadata().has_perf_command_line_whole()) {
      string perf_command =
          "perf-command:" +
          perf_data_.string_metadata().perf_command_line_whole().value();
      profile->add_comment(UTF8StringId(perf_command, builder));
    }
  } else {
    Profile* profile = per_pid.builder->mutable_profile();
    if ((options_ & kGroupByPids) && sample.main_mapping != nullptr &&
        sample.main_mapping->filename != nullptr) {
      const string& filename =
          profile->string_table(profile->mapping(0).filename());
      const string& sample_filename = MappingFilename(sample.main_mapping);

      if (filename != sample_filename) {
        if (options_ & kFailOnMainMappingMismatch) {
          LOG(FATAL) << "main mapping mismatch: " << sample.sample.pid() << " "
                     << filename << " " << sample_filename;
        } else {
          LOG(WARNING) << "main mapping mismatch: " << sample.sample.pid()
                       << " " << filename << " " << sample_filename;
        }
      }
    }
  }
  if (sample.sample.sample_time_ns()) {
    per_pid.process_meta->UpdateTimestamps(sample.sample.sample_time_ns());
  }
  return per_pid.builder;
}

uint64 PerfDataConverter::AddOrGetMapping(const Pid& pid,
                                          const PerfDataHandler::Mapping* smap,
                                          ProfileBuilder* builder) {
  if (builder == nullptr) {
    std::cerr << "Cannot add mapping to null builder." << std::endl;
    abort();
  }

  if (smap == nullptr) {
    return 0;
  }

  MappingMap& mapmap = per_pid_[pid].mapping_map;
  auto it = mapmap.find(smap);
  if (it != mapmap.end()) {
    return it->second;
  }

  Profile* profile = builder->mutable_profile();
  auto mapping = profile->add_mapping();
  uint64 mapping_id = profile->mapping_size();
  mapping->set_id(mapping_id);
  mapping->set_memory_start(smap->start);
  mapping->set_memory_limit(smap->limit);
  mapping->set_file_offset(smap->file_offset);
  if (smap->build_id != nullptr && !smap->build_id->empty()) {
    mapping->set_build_id(UTF8StringId(*smap->build_id, builder));
  }
  mapping->set_filename(UTF8StringId(MappingFilename(smap), builder));
  if (mapping->memory_start() >= mapping->memory_limit()) {
    std::cerr << "The start of the mapping must be strictly less than its"
              << "limit in file: " << mapping->filename() << std::endl
              << "Start: " << mapping->memory_start() << std::endl
              << "Limit: " << mapping->memory_limit() << std::endl;
    abort();
  }
  mapmap.insert(std::make_pair(smap, mapping_id));
  return mapping_id;
}

void PerfDataConverter::AddOrUpdateSample(
    const PerfDataHandler::SampleContext& context, const Pid& pid,
    const SampleKey& sample_key,
    ProfileBuilder* builder) {

  perftools::profiles::Sample* sample = per_pid_[pid].sample_map[sample_key];

  if (sample == nullptr) {
    Profile* profile = builder->mutable_profile();
    sample = profile->add_sample();
    per_pid_[pid].sample_map[sample_key] = sample;
    for (const auto& location_id : sample_key.stack) {
      sample->add_location_id(location_id);
    }
    // Emit any requested labels.
    if (IncludePidLabels() && context.sample.has_pid()) {
      auto* label = sample->add_label();
      label->set_key(builder->StringId(PidLabelKey));
      label->set_num(static_cast<int64>(context.sample.pid()));
    }
    if (IncludeTidLabels() && context.sample.has_tid()) {
      auto* label = sample->add_label();
      label->set_key(builder->StringId(TidLabelKey));
      label->set_num(static_cast<int64>(context.sample.tid()));
    }
    if (IncludeCommLabels() && sample_key.comm != 0) {
      auto* label = sample->add_label();
      label->set_key(builder->StringId(CommLabelKey));
      label->set_str(sample_key.comm);
    }
    if (IncludeTimestampNsLabels() && context.sample.has_sample_time_ns()) {
      auto* label = sample->add_label();
      label->set_key(builder->StringId(TimestampNsLabelKey));
      int64 timestamp_ns_as_int64 =
          static_cast<int64>(context.sample.sample_time_ns());
      label->set_num(timestamp_ns_as_int64);
    }
    if (IncludeExecutionModeLabels() && sample_key.exec_mode != Unknown) {
      auto* label = sample->add_label();
      label->set_key(builder->StringId(ExecutionModeLabelKey));
      label->set_str(builder->StringId(ExecModeString(sample_key.exec_mode)));
    }
    // Two values per collected event: the first is sample counts, the second is
    // event counts (unsampled weight for each sample).
    for (int event_id = 0; event_id < perf_data_.file_attrs_size();
         ++event_id) {
      sample->add_value(0);
      sample->add_value(0);
    }
  }

  int64 weight = 1;
  // If the sample has a period, use that in preference
  if (context.sample.period() > 0) {
    weight = context.sample.period();
  } else if (context.file_attrs_index >= 0) {
    uint64 period =
        perf_data_.file_attrs(context.file_attrs_index).attr().sample_period();
    if (period > 0) {
      // If sampling used a fixed period, use that as the weight.
      weight = period;
    }
  }
  int event_index = context.file_attrs_index;
  sample->set_value(2 * event_index, sample->value(2 * event_index) + 1);
  sample->set_value(2 * event_index + 1,
                    sample->value(2 * event_index + 1) + weight);
}

uint64 PerfDataConverter::AddOrGetLocation(
    const Pid& pid, uint64 addr, const PerfDataHandler::Mapping* mapping,
    ProfileBuilder* builder) {
  LocationMap& loc_map = per_pid_[pid].location_map;
  auto loc_it = loc_map.find(addr);
  if (loc_it != loc_map.end()) {
    return loc_it->second;
  }

  Profile* profile = builder->mutable_profile();
  perftools::profiles::Location* loc = profile->add_location();
  uint64 loc_id = profile->location_size();
  loc->set_id(loc_id);
  loc->set_address(addr);
  uint64 mapping_id = AddOrGetMapping(pid, mapping, builder);
  if (mapping_id != 0) {
    loc->set_mapping_id(mapping_id);
  } else {
    if (addr != 0) {
      std::cerr << "Found unmapped address: " << addr << " in PID " << pid
                << std::endl;
      abort();
    }
  }
  loc_map[addr] = loc_id;
  return loc_id;
}

void PerfDataConverter::Comm(const CommContext& comm) {
  Pid pid = comm.comm->pid();
  Pid tid = comm.comm->tid();
  if (pid == tid) {
    // pid==tid means an exec() happened, so clear everything from the
    // existing pid.
    per_pid_[pid].clear();
  }

  per_pid_[pid].tid_to_comm_map[tid] = comm.comm->comm();
}

// Invalidates the locations in location_map in the mmap event's range.
void PerfDataConverter::MMap(const MMapContext& mmap) {
  LocationMap& loc_map = per_pid_[mmap.pid].location_map;
  loc_map.erase(loc_map.lower_bound(mmap.mapping->start),
                loc_map.lower_bound(mmap.mapping->limit));
}

void PerfDataConverter::Sample(const PerfDataHandler::SampleContext& sample) {
  if (sample.file_attrs_index < 0 ||
      sample.file_attrs_index >= perf_data_.file_attrs_size()) {
    LOG(WARNING) << "out of bounds file_attrs_index: "
                 << sample.file_attrs_index;
    return;
  }

  Pid event_pid = sample.sample.pid();
  ProfileBuilder *builder = GetOrCreateBuilder(sample);
  SampleKey sample_key = MakeSampleKey(sample, builder);

  uint64 ip = sample.sample_mapping != nullptr ? sample.sample.ip() : 0;
  if (ip != 0) {
    const auto start = sample.sample_mapping->start;
    const auto limit = sample.sample_mapping->limit;
    if (ip < start || ip >= limit) {
      std::cerr << "IP is out of bound of mapping." << std::endl
                << "IP: " << ip << std::endl
                << "Start: " << start << std::endl
                << "Limit: " << limit << std::endl;
    }
  }

  // Leaf at stack[0]
  sample_key.stack.push_back(
      AddOrGetLocation(event_pid, ip, sample.sample_mapping, builder));

  // LBR callstacks include only user call chains. If this is an LBR sample,
  // we get the kernel callstack from the sample's callchain, and the user
  // callstack from the sample's branch_stack.
  const bool lbr_sample = !sample.branch_stack.empty();
  bool skipped_dup = false;
  for (const auto& frame : sample.callchain) {
    if (lbr_sample && frame.ip == PERF_CONTEXT_USER) {
      break;
    }
    if (!skipped_dup && sample_key.stack.size() == 1 && frame.ip == ip) {
      skipped_dup = true;
      // Newer versions of perf_events include the IP at the leaf of
      // the callchain.
      continue;
    }
    if (frame.mapping == nullptr) {
      continue;
    }
    uint64 frame_ip = frame.ip;
    // Why <=? Because this is a return address, which should be
    // preceded by a call (the "real" context.)  If we're at the edge
    // of the mapping, we're really off its edge.
    if (frame_ip <= frame.mapping->start) {
      continue;
    }
    // these aren't real callchain entries, just hints as to kernel/user
    // addresses.
    if (frame_ip >= PERF_CONTEXT_MAX) {
      continue;
    }

    // subtract one so we point to the call instead of the return addr.
    frame_ip--;
    sample_key.stack.push_back(
        AddOrGetLocation(event_pid, frame_ip, frame.mapping, builder));
  }
  for (const auto& frame : sample.branch_stack) {
    // branch_stack entries are pairs of <from, to> locations corresponding to
    // addresses of call instructions and target addresses of those calls.
    // We need only the addresses of the function call instructions, stored in
    // the 'from' field, to recover the call chains.
    if (frame.from.mapping == nullptr) {
      continue;
    }
    // An LBR entry includes the address of the call instruction, so we don't
    // have to do any adjustments.
    if (frame.from.ip < frame.from.mapping->start) {
      continue;
    }
    sample_key.stack.push_back(AddOrGetLocation(event_pid, frame.from.ip,
                                                frame.from.mapping, builder));
  }
  AddOrUpdateSample(sample, event_pid, sample_key, builder);
}

ProcessProfiles PerfDataConverter::Profiles() {
  ProcessProfiles pps;
  for (int i = 0; i < builders_.size(); i++) {
    auto& b = builders_[i];
    b.Finalize();
    auto pp = process_metas_[i].makeProcessProfile(b.mutable_profile());
    pps.push_back(std::move(pp));
  }
  return pps;
}

}  // namespace

ProcessProfiles PerfDataProtoToProfiles(const quipper::PerfDataProto* perf_data,
                                        const uint32 sample_labels,
                                        const uint32 options) {
  PerfDataConverter converter(*perf_data, sample_labels, options);
  PerfDataHandler::Process(*perf_data, &converter);
  return converter.Profiles();
}

ProcessProfiles RawPerfDataToProfiles(const void* raw, const int raw_size,
                                      const std::map<string, string>& build_ids,
                                      const uint32 sample_labels,
                                      const uint32 options) {
  quipper::PerfReader reader;
  if (!reader.ReadFromPointer(reinterpret_cast<const char*>(raw), raw_size)) {
    LOG(ERROR) << "Could not read input perf.data";
    return ProcessProfiles();
  }

  reader.InjectBuildIDs(build_ids);
  // Perf populates info about the kernel using multiple pathways,
  // which don't actually all match up how they name kernel data; in
  // particular, buildids are reported by a different name than the
  // actual "mmap" info.  Normalize these names so our ProcessProfiles
  // will match kernel mappings to a buildid.
  reader.LocalizeUsingFilenames({
      {"[kernel.kallsyms]_text", "[kernel.kallsyms]"},
      {"[kernel.kallsyms]_stext", "[kernel.kallsyms]"},
  });

  // Use PerfParser to modify reader's events to have magic done to them such
  // as hugepage deduction and sorting events based on time, if timestamps are
  // present.
  quipper::PerfParserOptions opts;
  opts.sort_events_by_time = true;
  opts.deduce_huge_page_mappings = true;
  opts.combine_mappings = true;
  quipper::PerfParser parser(&reader, opts);
  if (!parser.ParseRawEvents()) {
    LOG(ERROR) << "Could not parse perf events.";
    return ProcessProfiles();
  }

  return PerfDataProtoToProfiles(&reader.proto(), sample_labels, options);
}

}  // namespace perftools
