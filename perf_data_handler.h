/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PERF_DATA_HANDLER_H_
#define PERFTOOLS_PERF_DATA_HANDLER_H_

#include <vector>

#include "int_compat.h"
#include "string_compat.h"
#include "quipper/perf_data.pb.h"

namespace perftools {

// PerfDataHandler defines an interface for processing PerfDataProto
// with normalized sample fields (i.e., materializing mappings,
// filenames, and build-ids).
//
// To use, subclass PerfDataHandler and implement the required
// methods, then call Process() and handler will be called for every
// SAMPLE event.
//
// Context events' pointers to Mappings will be constant for the lifetime of a
// process, so subclasses may use the pointer values as a key to various caches
// they may want to maintain as part of the output data creation.
class PerfDataHandler {
 public:
  struct Mapping {
   public:
    Mapping(const string* filename, const string* build_id, uint64 start,
            uint64 limit, uint64 file_offset, uint64 filename_md5_prefix)
        : filename(filename),
          build_id(build_id),
          start(start),
          limit(limit),
          file_offset(file_offset),
          filename_md5_prefix(filename_md5_prefix) {}

    // filename and build_id are pointers into the provided
    // PerfDataProto and may be nullptr.
    const string* filename;
    const string* build_id;
    uint64 start;
    uint64 limit;  // limit=ceiling.
    uint64 file_offset;
    uint64 filename_md5_prefix;

   private:
    Mapping() {}
  };

  struct Location {
    Location() : ip(0), mapping(nullptr) {}

    uint64 ip;
    const Mapping* mapping;
  };

  struct BranchStackPair {
    BranchStackPair() : mispredicted(false) {}

    Location from;
    Location to;
    bool mispredicted;
  };

  struct SampleContext {
    SampleContext(const quipper::PerfDataProto::EventHeader &h,
                  const quipper::PerfDataProto::SampleEvent &s)
        : header(h),
          sample(s),
          main_mapping(nullptr),
          sample_mapping(nullptr),
          file_attrs_index(-1) {}

    // The event's header.
    const quipper::PerfDataProto::EventHeader &header;
    // An event.
    const quipper::PerfDataProto::SampleEvent &sample;
    // The mapping for the main binary for this program.
    const Mapping* main_mapping;
    // The mapping in which event.ip is found.
    const Mapping* sample_mapping;
    // Locations corresponding to event.callchain.
    std::vector<Location> callchain;
    // Locations corresponding to entries in event.branch_stack.
    std::vector<BranchStackPair> branch_stack;
    // An index into PerfDataProto.file_attrs or -1 if
    // unavailable.
    int64 file_attrs_index;
  };

  struct CommContext {
    // A comm event.
    const quipper::PerfDataProto::CommEvent* comm;
  };

  struct MMapContext {
    // A memory mapping to be passed to the subclass. Should be the same mapping
    // that gets added to pid_to_mmaps_.
    const PerfDataHandler::Mapping* mapping;
    // The process id used as a key to pid_to_mmaps_.
    uint32 pid;
  };

  PerfDataHandler(const PerfDataHandler&) = delete;
  PerfDataHandler& operator=(const PerfDataHandler&) = delete;

  // Process initiates processing of perf_proto.  handler.Sample will
  // be called for every event in the profile.
  static void Process(const quipper::PerfDataProto& perf_data,
                      PerfDataHandler* handler);

  virtual ~PerfDataHandler() {}

  // Implement these callbacks:
  // Called for every sample.
  virtual void Sample(const SampleContext& sample) = 0;
  // When comm.pid()==comm.tid() it indicates an exec() happened.
  virtual void Comm(const CommContext& comm) = 0;
  // Called for every mmap event.
  virtual void MMap(const MMapContext& mmap) = 0;

 protected:
  PerfDataHandler();
};

}  // namespace perftools

#endif  // PERFTOOLS_PERF_DATA_HANDLER_H_
