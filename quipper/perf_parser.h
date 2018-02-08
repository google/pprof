// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_PARSER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_PARSER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/macros.h"

#include "binary_data_utils.h"
#include "compat/proto.h"
#include "compat/string.h"
#include "dso.h"
#include "perf_reader.h"

namespace quipper {

using PerfEvent = PerfDataProto_PerfEvent;

// PID associated with the kernel mmap event.
const uint32_t kKernelPid = static_cast<uint32_t>(-1);

class AddressMapper;
class PerfDataProto_BranchStackEntry;
class PerfDataProto_CommEvent;
class PerfDataProto_ForkEvent;
class PerfDataProto_MMapEvent;
class PerfDataProto_PerfEvent;

struct ParsedEvent {
  ParsedEvent() : command_(NULL) {}

  // Stores address of the original PerfDataProto_PerfEvent owned by a
  // PerfReader object.
  PerfDataProto_PerfEvent* event_ptr;

  // For mmap events, use this to count the number of samples that are in this
  // region.
  uint32_t num_samples_in_mmap_region;

  // Command associated with this sample.
  const string* command_;

  // Accessor for command string.
  const string command() const {
    if (command_) return *command_;
    return string();
  }

  void set_command(const string* command) { command_ = command; }

  // A struct that contains a DSO + offset pair.
  struct DSOAndOffset {
    const DSOInfo* dso_info_;
    uint64_t offset_;

    // Accessor methods.
    const string dso_name() const {
      if (dso_info_) return dso_info_->name;
      return string();
    }
    const string build_id() const {
      if (dso_info_) return dso_info_->build_id;
      return string();
    }
    uint64_t offset() const { return offset_; }

    DSOAndOffset() : dso_info_(NULL), offset_(0) {}

    bool operator==(const DSOAndOffset& other) const {
      return offset_ == other.offset_ &&
             !dso_name().compare(other.dso_name()) &&
             !build_id().compare(other.build_id());
    }
  } dso_and_offset;

  // DSO + offset info for callchain.
  std::vector<DSOAndOffset> callchain;

  // DSO + offset info for branch stack entries.
  struct BranchEntry {
    bool predicted;
    DSOAndOffset from;
    DSOAndOffset to;

    bool operator==(const BranchEntry& other) const {
      return predicted == other.predicted && from == other.from &&
             to == other.to;
    }
  };
  std::vector<BranchEntry> branch_stack;

  // For comparing ParsedEvents.
  bool operator==(const ParsedEvent& other) const {
    return dso_and_offset == other.dso_and_offset &&
           std::equal(callchain.begin(), callchain.end(),
                      other.callchain.begin()) &&
           std::equal(branch_stack.begin(), branch_stack.end(),
                      other.branch_stack.begin());
  }
};

struct PerfEventStats {
  // Number of each type of event.
  uint32_t num_sample_events;
  uint32_t num_mmap_events;
  uint32_t num_comm_events;
  uint32_t num_fork_events;
  uint32_t num_exit_events;

  // Number of sample events that were successfully mapped using the address
  // mapper.  The mapping is recorded regardless of whether the address in the
  // perf sample event itself was assigned the remapped address.  The latter is
  // indicated by |did_remap|.
  uint32_t num_sample_events_mapped;

  // Whether address remapping was enabled during event parsing.
  bool did_remap;
};

struct PerfParserOptions {
  // For synthetic address mapping.
  bool do_remap = false;
  // Set this flag to discard non-sample events that don't have any associated
  // sample events. e.g. MMAP regions with no samples in them.
  bool discard_unused_events = false;
  // When mapping perf sample events, at least this percentage of them must be
  // successfully mapped in order for ProcessEvents() to return true.
  // By default, most samples must be properly mapped in order for sample
  // mapping to be considered successful.
  float sample_mapping_percentage_threshold = 95.0f;
  // Set this to sort perf events by time, assuming they have timestamps.
  // PerfSerializer::serialize_sorted_events_, which is used by
  // PerfSerializerTest. However, we should look at restructuring PerfParser not
  // to need it, while still providing some PerfParserStats.
  bool sort_events_by_time = true;
  // If buildids are missing from the input data, they can be retrieved from
  // the filesystem.
  bool read_missing_buildids = false;
  // Deduces file names and offsets for hugepage-backed mappings, as
  // hugepage_text replaces these with anonymous mappings without filename or
  // offset information..
  bool deduce_huge_page_mappings = true;
  // Checks for split binary mappings and merges them when possible.  This
  // combines the split mappings into a single mapping so future consumers of
  // the perf data will see  a single mapping and not two or more distinct
  // mappings.
  bool combine_mappings = true;
};

class PerfParser {
 public:
  explicit PerfParser(PerfReader* reader);
  ~PerfParser();

  // Constructor that takes in options at PerfParser creation time.
  explicit PerfParser(PerfReader* reader, const PerfParserOptions& options);

  // Pass in a struct containing various options.
  void set_options(const PerfParserOptions& options) { options_ = options; }

  // Gets parsed event/sample info from raw event data. Stores pointers to the
  // raw events in an array of ParsedEvents. Does not own the raw events. It is
  // up to the user of this class to keep track of when these event pointers are
  // invalidated.
  bool ParseRawEvents();

  const std::vector<ParsedEvent>& parsed_events() const {
    return parsed_events_;
  }

  const PerfEventStats& stats() const { return stats_; }

  // Use with caution. Deserialization uses this to restore stats from proto.
  PerfEventStats* mutable_stats() { return &stats_; }

 private:
  // Used for processing events.  e.g. remapping with synthetic addresses.
  bool ProcessEvents();

  // Used for processing user events.
  bool ProcessUserEvents(PerfEvent& event);

  // Looks up build IDs for all DSOs present in |reader_| by direct lookup using
  // functions in dso.h. If there is a DSO with both an existing build ID and a
  // new build ID read using dso.h, this will overwrite the existing build ID.
  bool FillInDsoBuildIds();

  // Updates |reader_->events| based on the contents of |parsed_events_|. For
  // example, if |parsed_events_| had some events removed or reordered,
  // |reader_| would be updated to contain the new sequence of events.
  void UpdatePerfEventsFromParsedEvents();

  // Does a sample event remap and then returns DSO name and offset of sample.
  bool MapSampleEvent(ParsedEvent* parsed_event);

  // Calls MapIPAndPidAndGetNameAndOffset() on the callchain of a sample event.
  bool MapCallchain(const uint64_t ip, const PidTid pidtid,
                    uint64_t original_event_addr,
                    RepeatedField<uint64>* callchain,
                    ParsedEvent* parsed_event);

  // Trims the branch stack for null entries and calls
  // MapIPAndPidAndGetNameAndOffset() on each entry.
  bool MapBranchStack(
      const PidTid pidtid,
      RepeatedPtrField<PerfDataProto_BranchStackEntry>* branch_stack,
      ParsedEvent* parsed_event);

  // This maps a sample event and returns the mapped address, DSO name, and
  // offset within the DSO.  This is a private function because the API might
  // change in the future, and we don't want derived classes to be stuck with an
  // obsolete API.
  bool MapIPAndPidAndGetNameAndOffset(
      uint64_t ip, const PidTid pidtid, uint64_t* new_ip,
      ParsedEvent::DSOAndOffset* dso_and_offset);

  // Parses a MMAP event. Adds the mapping to the AddressMapper of the event's
  // process. If |options_.do_remap| is set, will update |event| with the
  // remapped address.
  bool MapMmapEvent(PerfDataProto_MMapEvent* event, uint64_t id);

  // Processes a COMM event. Creates a new AddressMapper for the new command's
  // process.
  bool MapCommEvent(const PerfDataProto_CommEvent& event);

  // Processes a FORK event. Creates a new AddressMapper for the PID of the new
  // process, if none already exists.
  bool MapForkEvent(const PerfDataProto_ForkEvent& event);

  // Create a process mapper for a process. Optionally pass in a parent pid
  // |ppid| from which to copy mappings.
  // Returns (mapper, true) if a new AddressMapper was created, and
  // (mapper, false) if there is an existing mapper.
  std::pair<AddressMapper*, bool> GetOrCreateProcessMapper(
      uint32_t pid, uint32_t ppid = kKernelPid);

  // Points to a PerfReader that contains the input perf data to parse.
  PerfReader* const reader_;

  // Stores the output of ParseRawEvents(). Contains DSO + offset info for each
  // event.
  std::vector<ParsedEvent> parsed_events_;

  // Store all option flags as one struct.
  PerfParserOptions options_;

  // Maps pid/tid to commands.
  std::map<PidTid, const string*> pidtid_to_comm_map_;

  // A set to store the actual command strings.
  std::set<string> commands_;

  // ParseRawEvents() records some statistics here.
  PerfEventStats stats_;

  // A set of unique DSOs that may be referenced by multiple events.
  std::unordered_map<string, DSOInfo> name_to_dso_;

  // Maps process ID to an address mapper for that process.
  std::unordered_map<uint32_t, std::unique_ptr<AddressMapper>> process_mappers_;

  DISALLOW_COPY_AND_ASSIGN(PerfParser);
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_PARSER_H_
