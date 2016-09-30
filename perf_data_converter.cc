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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND
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

#include "perf_data_converter.h"

#include <map>
#include <sstream>
#include <vector>

#include "builder.h"
#include "chromiumos-wide-profiling/perf_data.pb.h"
#include "chromiumos-wide-profiling/perf_parser.h"
#include "chromiumos-wide-profiling/perf_reader.h"
#include "int_compat.h"
#include "intervalmap.h"
#include "perf_data_handler.h"
#include "string_compat.h"

namespace perftools {
namespace {

typedef perftools::profiles::Profile Profile;

typedef perftools::profiles::Builder ProfileBuilder;

typedef uint32 Pid;
typedef std::vector<std::unique_ptr<Profile>> ProfileVector;
typedef std::map<std::vector<uint64>, perftools::profiles::Sample*> SampleMap;
typedef std::unordered_map<uint64, perftools::profiles::Location*> LocationMap;
typedef IntervalMap<perftools::profiles::Mapping*> MappingMap;

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

// It is sufficient to key the caches for Locations and Mappings by PID.
// However, when Samples include labels, it is necessary to key their caches
// not only by PID, but also by anything their labels may contain, since labels
// are also distinguishing features.  This struct should contain everything
// required to uniquely identify a Sample: if two Samples you consider different
// end up with the same SampleKey, you should extend SampleKey til they don't.
//
// If any of these values are not used as labels, they should be set to 0.
struct SampleKey {
 public:
  Pid pid;
  Pid tid;
  uint64 time_ns;
  ExecutionMode exec_mode;
  SampleKey() {
    pid = 0;
    tid = 0;
    time_ns = 0;
    exec_mode = Unknown;
  }
};

struct SampleKeyEqualityTester {
  bool operator()(const SampleKey a, const SampleKey b) const {
    return ((a.pid == b.pid) && (a.tid == b.tid) && (a.time_ns == b.time_ns) &&
            (a.exec_mode == b.exec_mode));
  }
};

struct SampleKeyHasher {
  size_t operator()(const SampleKey k) const {
    return (std::hash<int32>()(k.pid) ^ std::hash<int32>()(k.tid) ^
            std::hash<uint64>()(k.time_ns) ^ std::hash<int>()(k.exec_mode));
  }
};

class PerfDataConverter : public PerfDataHandler {
 public:
  explicit PerfDataConverter(const quipper::PerfDataProto& perf_data,
                             uint32 sample_labels = kNoLabels,
                             bool group_by_pids = true)
      : perf_data_(perf_data),
        sample_labels_(sample_labels),
        group_by_pids_(group_by_pids) {}
  PerfDataConverter(const PerfDataConverter&) = delete;
  PerfDataConverter& operator=(const PerfDataConverter&) = delete;
  virtual ~PerfDataConverter() {}

  ProfileVector Profiles();

  // Callbacks for PerfDataHandler
  virtual void Sample(const PerfDataHandler::SampleContext& sample);
  virtual void Comm(const CommContext& comm);

 private:
  void AddSample(const PerfDataHandler::SampleContext& context, const Pid& pid,
                 const SampleKey& sample_key, const std::vector<uint64>& stack,
                 ProfileBuilder* builder);
  perftools::profiles::Location* AddLocation(const Pid& pid, uint64 address,
                                             ProfileBuilder* builder);
  void AddMapping(const Pid& pid, uint64 for_ip,
                  const PerfDataHandler::Mapping* smap_in,
                  ProfileBuilder* builder);
  perftools::profiles::Mapping* LookupMapping(const Pid& pid, uint64 address,
                                              ProfileBuilder* builder);

  bool IncludePidLabels() { return (sample_labels_ & kPidLabel); }
  bool IncludeTidLabels() { return (sample_labels_ & kTidLabel); }
  bool IncludeTimestampNsLabels() {
    return (sample_labels_ & kTimestampNsLabel);
  }
  bool IncludeExecutionModeLabels() {
    return (sample_labels_ & kExecutionModeLabel);
  }

  const quipper::PerfDataProto& perf_data_;
  std::vector<std::unique_ptr<ProfileBuilder>> builders_;

  std::unordered_map<Pid, ProfileBuilder*> builder_cache_;
  std::unordered_map<Pid, LocationMap> location_cache_;
  std::unordered_map<Pid, MappingMap> mapping_cache_;

  // While Locations and Mappings are per-address-space (=per-process), samples
  // can be thread-specific.  If the requested sample labels include PID and
  // TID, we'll need to cache separate profile_proto::Samples for samples that
  // are identical except for TID.  Likewise, if the requested sample labels
  // include timestamp_ns, then we'll need to have separate
  // profile_proto::Samples for samples that are identical except for timestamp.
  std::unordered_map<SampleKey, SampleMap, SampleKeyHasher,
                     SampleKeyEqualityTester>
      sample_cache_;

  const uint32 sample_labels_;

  const bool group_by_pids_ = true;
};

void PerfDataConverter::AddMapping(const Pid& pid, uint64 for_ip,
                                   const PerfDataHandler::Mapping* smap,
                                   ProfileBuilder* builder) {
  if (builder == nullptr) {
    std::cerr << "Cannot add mapping to null builder." << std::endl;
    abort();
  }
  if (smap == nullptr) {
    return;
  }
  auto mapit = mapping_cache_.find(pid);
  if (mapit == mapping_cache_.end()) {
    mapping_cache_[pid] = MappingMap();
    mapit = mapping_cache_.find(pid);
  }
  auto mapmap = &mapit->second;
  perftools::profiles::Mapping* mapping = nullptr;
  if (!mapmap->Lookup(for_ip, &mapping)) {
    Profile* profile = builder->mutable_profile();
    auto mapping = profile->add_mapping();
    mapping->set_id(profile->mapping_size());
    mapping->set_memory_start(smap->start);
    mapping->set_memory_limit(smap->limit);
    mapping->set_file_offset(smap->file_offset);
    if (smap->build_id != nullptr && !smap->build_id->empty()) {
      mapping->set_build_id(builder->StringId(smap->build_id->c_str()));
    }
    if (smap->filename != nullptr && !smap->filename->empty()) {
      mapping->set_filename(builder->StringId(smap->filename->c_str()));
    } else if (smap->filename_md5_prefix != 0) {
      std::stringstream filename;
      filename << std::hex << smap->filename_md5_prefix;
      mapping->set_filename(builder->StringId(filename.str().c_str()));
    }
    if (mapping->memory_start() >= mapping->memory_limit()) {
      std::cerr << "The start of the mapping must be strictly less than its"
                << "limit in file: " << mapping->filename() << std::endl
                << "Start: " << mapping->memory_start() << std::endl
                << "Limit: " << mapping->memory_limit() << std::endl;
      abort();
    }
    mapmap->Set(mapping->memory_start(), mapping->memory_limit(), mapping);
  }
}

void PerfDataConverter::AddSample(const PerfDataHandler::SampleContext& context,
                                  const Pid& pid, const SampleKey& sample_key,
                                  const std::vector<uint64>& stack,
                                  ProfileBuilder* builder) {
  perftools::profiles::Sample* sample = nullptr;
  auto &sample_key_cache = sample_cache_[sample_key];

  auto sampit = sample_key_cache.find(stack);
  if (sampit != sample_key_cache.end()) {
    sample = sampit->second;
  }

  if (sample == nullptr) {
    Profile* profile = builder->mutable_profile();
    sample = profile->add_sample();
    sample_key_cache[stack] = sample;

    for (const auto& addr : stack) {
      auto &lcpid = location_cache_[pid];
      auto lcit = lcpid.find(addr);
      if (lcit != lcpid.end()) {
        sample->add_location_id(lcit->second->id());
        continue;
      }
      perftools::profiles::Location *location = AddLocation(pid, addr, builder);
      if (location == nullptr) {
        std::cerr << "AddLocation failed." << std::endl;
        abort();
      }
      if (location->address() != addr) {
        std::cerr << "Added location has inconsistent address." << std::endl
                  << "Expected: " << addr << std::endl
                  << "Found: " << location->address() << std::endl;
        abort();
      }
      sample->add_location_id(location->id());
    }
    // Emit any requested labels.
    if (IncludePidLabels() && context.sample.has_pid()) {
      auto label = sample->add_label();
      label->set_key(builder->StringId(PidLabelKey));
      label->set_num(static_cast<int64>(context.sample.pid()));
    }
    if (IncludeTidLabels() && context.sample.has_tid()) {
      auto label = sample->add_label();
      label->set_key(builder->StringId(TidLabelKey));
      label->set_num(static_cast<int64>(context.sample.tid()));
    }
    if (IncludeTimestampNsLabels() && context.sample.has_sample_time_ns()) {
      auto label = sample->add_label();
      label->set_key(builder->StringId(TimestampNsLabelKey));
      int64 timestamp_ns_as_int64 =
          static_cast<int64>(context.sample.sample_time_ns());
      label->set_num(timestamp_ns_as_int64);
    }
    if (IncludeExecutionModeLabels() && sample_key.exec_mode != Unknown) {
      auto label = sample->add_label();
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

perftools::profiles::Mapping* PerfDataConverter::LookupMapping(
    const Pid& pid, uint64 address, ProfileBuilder* builder) {
  MappingMap mapmap = mapping_cache_[pid];
  perftools::profiles::Mapping* mapping = nullptr;
  mapmap.Lookup(address, &mapping);
  return mapping;
}

perftools::profiles::Location* PerfDataConverter::AddLocation(
    const Pid& pid, uint64 address, ProfileBuilder* builder) {
  Profile* profile = builder->mutable_profile();
  perftools::profiles::Location* location = profile->add_location();
  location->set_id(profile->location_size());
  location->set_address(address);

  auto mapping = LookupMapping(pid, address, builder);
  if (mapping != nullptr) {
    location->set_mapping_id(mapping->id());
  } else {
    if (address != 0) {
      std::cerr << "Found unmapped address: " << address << " in PID " << pid
                << std::endl;
      abort();
    }
  }
  location_cache_[pid][address] = location;
  return location;
}

void PerfDataConverter::Comm(const CommContext& comm) {
  if (comm.comm->pid() == comm.comm->tid()) {
    // pid==tid means an exec() happened, so clear everything from the
    // existing pid.

    Pid pid = comm.comm->pid();
    builder_cache_[pid] = nullptr;
    location_cache_[pid].clear();
    mapping_cache_[pid].Clear();

    // Every Sample in the cache with this PID gets wiped.
    for (auto samples_it = sample_cache_.begin();
         samples_it != sample_cache_.end(); ++samples_it) {
      if (samples_it->first.pid == comm.comm->pid()) {
        samples_it->second.clear();
      }
    }
  }
}

void PerfDataConverter::Sample(const PerfDataHandler::SampleContext& sample) {
  if (sample.file_attrs_index < 0 ||
      sample.file_attrs_index >= perf_data_.file_attrs_size()) {
    LOG(WARNING) << "out of bounds file_attrs_index: "
                 << sample.file_attrs_index;
    return;
  }

  Pid builder_pid = group_by_pids_ ? sample.sample.pid() : 0;
  Pid event_pid = sample.sample.pid();
  SampleKey sample_key;
  sample_key.pid = sample.sample.has_pid() ? sample.sample.pid() : 0;
  sample_key.tid =
      (IncludeTidLabels() && sample.sample.has_tid()) ? sample.sample.tid() : 0;
  sample_key.time_ns =
      (IncludeTimestampNsLabels() && sample.sample.has_sample_time_ns())
          ? sample.sample.sample_time_ns()
          : 0;
  if (IncludeExecutionModeLabels() && sample.header.has_misc()) {
    switch (sample.header.misc()) {
      case PERF_RECORD_MISC_KERNEL:
        sample_key.exec_mode = HostKernel;
        break;
      case PERF_RECORD_MISC_USER:
        sample_key.exec_mode = HostUser;
        break;
      case PERF_RECORD_MISC_GUEST_KERNEL:
        sample_key.exec_mode = GuestKernel;
        break;
      case PERF_RECORD_MISC_GUEST_USER:
        sample_key.exec_mode = GuestUser;
        break;
      case PERF_RECORD_MISC_HYPERVISOR:
        sample_key.exec_mode = Hypervisor;
        break;
    }
  }

  ProfileBuilder* builder = nullptr;
  auto bcit = builder_cache_.find(builder_pid);
  if (bcit != builder_cache_.end()) {
    builder = bcit->second;
  }
  if (builder == nullptr) {
    builder = new ProfileBuilder();
    builder_cache_[builder_pid] = builder;
    builders_.emplace_back(builder);
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
      sample_type->set_type(builder->StringId((event_name + "sample").c_str()));
      sample_type->set_unit(builder->StringId("count"));
      sample_type = profile->add_sample_type();
      sample_type->set_type(builder->StringId((event_name + "event").c_str()));
      sample_type->set_unit(builder->StringId("count"));
    }
    if (sample.main_mapping == nullptr) {
      auto fake_main = profile->add_mapping();
      fake_main->set_id(profile->mapping_size());
      fake_main->set_memory_start(0);
      fake_main->set_memory_limit(1);
    } else {
      AddMapping(event_pid, sample.main_mapping->limit - 1, sample.main_mapping,
                 builder);
    }
  }

  if (!sample.branch_stack.empty()) {
    std::cerr << "don't know how to handle branch_stack" << std::endl;
    abort();
  }

  if (sample.branch_stack.empty()) {
    // Normal sample or has callchain.
    std::vector<uint64> stack;
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
    AddMapping(event_pid, ip, sample.sample_mapping, builder);

    stack.push_back(ip);  // Leaf at stack[0]
    bool skipped_dup = false;
    for (const auto& frame : sample.callchain) {
      if (!skipped_dup && stack.size() == 1 && frame.ip == stack[0]) {
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
      if (frame.mapping != nullptr) {
        AddMapping(event_pid, frame_ip, frame.mapping, builder);
      }
      stack.push_back(frame_ip);
    }
    AddSample(sample, event_pid, sample_key, stack, builder);
  }
}

ProfileVector PerfDataConverter::Profiles() {
  ProfileVector profiles;
  for (auto& builder : builders_) {
    builder->Finalize();
    Profile* profile = new Profile();
    profile->Swap(builder->mutable_profile());
    profiles.emplace_back(std::unique_ptr<Profile>(profile));
  }

  return profiles;
}

ProfileVector PerfDataProtoToProfileList(quipper::PerfDataProto* perf_data,
                                         uint32 sample_labels,
                                         bool group_by_pids) {
  PerfDataConverter converter(*perf_data, sample_labels, group_by_pids);
  PerfDataHandler::Process(*perf_data, &converter);
  return converter.Profiles();
}

}  // namespace

ProfileVector RawPerfDataToProfileProto(const void* raw, int raw_size,
                                        const std::map<string, string> &build_id_map,
                                        uint32 sample_labels,
                                        bool group_by_pids) {
  std::unique_ptr<quipper::PerfReader> reader(new quipper::PerfReader);
  if (!reader->ReadFromPointer(reinterpret_cast<const char*>(raw), raw_size)) {
    LOG(ERROR) << "Could not read input perf.data";
    return ProfileVector();
  }

  std::unique_ptr<quipper::PerfParser> parser(
      new quipper::PerfParser(reader.get()));
  if (!parser->ParseRawEvents()) {
    LOG(ERROR) << "Could not parse input perf data";
    return ProfileVector();
  }

  reader->InjectBuildIDs(build_id_map);
  // Perf populates info about the kernel using multiple pathways,
  // which don't actually all match up how they name kernel data; in
  // particular, buildids are reported by a different name than the
  // actual "mmap" info.  Normalize these names so our ProfileVector
  // will match kernel mappings to a buildid.
  reader->LocalizeUsingFilenames({
      {"[kernel.kallsyms]_text", "[kernel.kallsyms]"},
      {"[kernel.kallsyms]_stext", "[kernel.kallsyms]"},
  });
  quipper::PerfDataProto perf_data;
  if (!reader->Serialize(&perf_data)) {
    LOG(ERROR) << "Could not serialize perf.data";
    return ProfileVector();
  }
  return PerfDataProtoToProfileList(&perf_data, sample_labels, group_by_pids);
}

ProfileVector SerializedPerfDataProtoToProfileProto(
    const string& serialized_perf_data, uint32 sample_labels,
    bool group_by_pids) {
  quipper::PerfDataProto perf_data;
  perf_data.ParseFromString(serialized_perf_data);
  return PerfDataProtoToProfileList(&perf_data, sample_labels, group_by_pids);
}

}  // namespace perftools
