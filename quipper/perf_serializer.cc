// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_serializer.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#include <algorithm>  // for std::copy

#include "base/logging.h"

#include "binary_data_utils.h"
#include "compat/proto.h"
#include "compat/string.h"
#include "kernel/perf_event.h"
#include "perf_data_structures.h"
#include "perf_data_utils.h"
#include "perf_parser.h"
#include "perf_reader.h"

namespace quipper {

PerfSerializer::PerfSerializer() {}

PerfSerializer::~PerfSerializer() {}

bool PerfSerializer::SerializePerfFileAttr(
    const PerfFileAttr& perf_file_attr,
    PerfDataProto_PerfFileAttr* perf_file_attr_proto) const {
  if (!SerializePerfEventAttr(perf_file_attr.attr,
                              perf_file_attr_proto->mutable_attr())) {
    return false;
  }

  for (size_t i = 0; i < perf_file_attr.ids.size(); i++)
    perf_file_attr_proto->add_ids(perf_file_attr.ids[i]);
  return true;
}

bool PerfSerializer::DeserializePerfFileAttr(
    const PerfDataProto_PerfFileAttr& perf_file_attr_proto,
    PerfFileAttr* perf_file_attr) const {
  if (!DeserializePerfEventAttr(perf_file_attr_proto.attr(),
                                &perf_file_attr->attr)) {
    return false;
  }

  for (int i = 0; i < perf_file_attr_proto.ids_size(); i++)
    perf_file_attr->ids.push_back(perf_file_attr_proto.ids(i));
  return true;
}

bool PerfSerializer::SerializePerfEventAttr(
    const perf_event_attr& perf_event_attr,
    PerfDataProto_PerfEventAttr* perf_event_attr_proto) const {
#define S(x) perf_event_attr_proto->set_##x(perf_event_attr.x)
  S(type);
  S(size);
  S(config);
  if (perf_event_attr_proto->freq())
    S(sample_freq);
  else
    S(sample_period);
  S(sample_type);
  S(read_format);
  S(disabled);
  S(inherit);
  S(pinned);
  S(exclusive);
  S(exclude_user);
  S(exclude_kernel);
  S(exclude_hv);
  S(exclude_idle);
  S(mmap);
  S(comm);
  S(freq);
  S(inherit_stat);
  S(enable_on_exec);
  S(task);
  S(watermark);
  S(precise_ip);
  S(mmap_data);
  S(sample_id_all);
  S(exclude_host);
  S(exclude_guest);
  S(exclude_callchain_kernel);
  S(exclude_callchain_user);
  S(mmap2);
  S(comm_exec);
  if (perf_event_attr_proto->watermark())
    S(wakeup_watermark);
  else
    S(wakeup_events);
  S(bp_type);
  S(bp_addr);
  S(bp_len);
  S(branch_sample_type);
  S(sample_regs_user);
  S(sample_stack_user);
#undef S
  return true;
}

bool PerfSerializer::DeserializePerfEventAttr(
    const PerfDataProto_PerfEventAttr& perf_event_attr_proto,
    perf_event_attr* perf_event_attr) const {
  memset(perf_event_attr, 0, sizeof(*perf_event_attr));
#define S(x) perf_event_attr->x = perf_event_attr_proto.x()
  S(type);
  S(size);
  S(config);
  if (perf_event_attr->freq)
    S(sample_freq);
  else
    S(sample_period);
  S(sample_type);
  S(read_format);
  S(disabled);
  S(inherit);
  S(pinned);
  S(exclusive);
  S(exclude_user);
  S(exclude_kernel);
  S(exclude_hv);
  S(exclude_idle);
  S(mmap);
  S(comm);
  S(freq);
  S(inherit_stat);
  S(enable_on_exec);
  S(task);
  S(watermark);
  S(precise_ip);
  S(mmap_data);
  S(sample_id_all);
  S(exclude_host);
  S(exclude_guest);
  S(exclude_callchain_kernel);
  S(exclude_callchain_user);
  S(mmap2);
  S(comm_exec);
  if (perf_event_attr->watermark)
    S(wakeup_watermark);
  else
    S(wakeup_events);
  S(bp_type);
  S(bp_addr);
  S(bp_len);
  S(branch_sample_type);
  S(sample_regs_user);
  S(sample_stack_user);
#undef S
  return true;
}

bool PerfSerializer::SerializePerfEventType(
    const PerfFileAttr& event_attr,
    quipper::PerfDataProto_PerfEventType* event_type_proto) const {
  event_type_proto->set_id(event_attr.attr.config);
  event_type_proto->set_name(event_attr.name);
  event_type_proto->set_name_md5_prefix(Md5Prefix(event_attr.name));
  return true;
}

bool PerfSerializer::DeserializePerfEventType(
    const quipper::PerfDataProto_PerfEventType& event_type_proto,
    PerfFileAttr* event_attr) const {
  // Attr should have already been deserialized.
  if (event_attr->attr.config != event_type_proto.id()) {
    LOG(ERROR) << "Event type ID " << event_type_proto.id()
               << " does not match attr.config " << event_attr->attr.config
               << ". Not deserializing the event name!";
    return false;
  }
  event_attr->name = event_type_proto.name();
  return true;
}

bool PerfSerializer::SerializeEvent(
    const malloced_unique_ptr<event_t>& event_ptr,
    PerfDataProto_PerfEvent* event_proto) const {
  const event_t& event = *event_ptr;

  if (!SerializeEventHeader(event.header, event_proto->mutable_header()))
    return false;

  if (event.header.type >= PERF_RECORD_USER_TYPE_START) {
    if (!SerializeUserEvent(event, event_proto)) {
      return false;
    }
  } else if (!SerializeKernelEvent(event, event_proto)) {
    return false;
  }

  event_proto->set_timestamp(GetTimeFromPerfEvent(*event_proto));
  return true;
}

bool PerfSerializer::SerializeKernelEvent(
    const event_t& event, PerfDataProto_PerfEvent* event_proto) const {
  switch (event.header.type) {
    case PERF_RECORD_SAMPLE:
      return SerializeSampleEvent(event, event_proto->mutable_sample_event());
    case PERF_RECORD_MMAP:
      return SerializeMMapEvent(event, event_proto->mutable_mmap_event());
    case PERF_RECORD_MMAP2:
      return SerializeMMap2Event(event, event_proto->mutable_mmap_event());
    case PERF_RECORD_COMM:
      return SerializeCommEvent(event, event_proto->mutable_comm_event());
    case PERF_RECORD_EXIT:
      return SerializeForkExitEvent(event, event_proto->mutable_exit_event());
    case PERF_RECORD_FORK:
      return SerializeForkExitEvent(event, event_proto->mutable_fork_event());
    case PERF_RECORD_LOST:
      return SerializeLostEvent(event, event_proto->mutable_lost_event());
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
      return SerializeThrottleEvent(event,
                                    event_proto->mutable_throttle_event());
    case PERF_RECORD_READ:
      return SerializeReadEvent(event, event_proto->mutable_read_event());
    case PERF_RECORD_AUX:
      return SerializeAuxEvent(event, event_proto->mutable_aux_event());
    default:
      LOG(ERROR) << "Unknown event type: " << event.header.type;
  }
  return true;
}

bool PerfSerializer::SerializeUserEvent(
    const event_t& event, PerfDataProto_PerfEvent* event_proto) const {
  switch (event.header.type) {
    case PERF_RECORD_AUXTRACE:
      return SerializeAuxtraceEvent(event,
                                    event_proto->mutable_auxtrace_event());
    default:
      if (event.header.type >= PERF_RECORD_HEADER_MAX) {
        LOG(ERROR) << "Unknown event type: " << event.header.type;
      }
  }
  return true;
}

bool PerfSerializer::DeserializeEvent(
    const PerfDataProto_PerfEvent& event_proto,
    malloced_unique_ptr<event_t>* event_ptr) const {
  event_ptr->reset(CallocMemoryForEvent(event_proto.header().size()));
  event_t* event = event_ptr->get();

  if (!DeserializeEventHeader(event_proto.header(), &event->header))
    return false;

  bool event_deserialized = false;
  if (event_proto.header().type() >= PERF_RECORD_USER_TYPE_START) {
    event_deserialized = DeserializeUserEvent(event_proto, event);
  } else {
    event_deserialized = DeserializeKernelEvent(event_proto, event);
  }

  if (!event_deserialized) {
    LOG(ERROR) << "Could not deserialize event of type "
               << event_proto.header().type();
    return false;
  }

  return true;
}

bool PerfSerializer::DeserializeKernelEvent(
    const PerfDataProto_PerfEvent& event_proto, event_t* event) const {
  switch (event_proto.header().type()) {
    case PERF_RECORD_SAMPLE:
      return DeserializeSampleEvent(event_proto.sample_event(), event);
    case PERF_RECORD_MMAP:
      return DeserializeMMapEvent(event_proto.mmap_event(), event);
    case PERF_RECORD_MMAP2:
      return DeserializeMMap2Event(event_proto.mmap_event(), event);
    case PERF_RECORD_COMM:
      return DeserializeCommEvent(event_proto.comm_event(), event);
    case PERF_RECORD_EXIT:
      return (event_proto.has_exit_event() &&
              DeserializeForkExitEvent(event_proto.exit_event(), event)) ||
             (event_proto.has_fork_event() &&
              DeserializeForkExitEvent(event_proto.fork_event(), event));
    // Some older protobufs use the |fork_event| field to store exit
    // events.
    case PERF_RECORD_FORK:
      return DeserializeForkExitEvent(event_proto.fork_event(), event);
    case PERF_RECORD_LOST:
      return DeserializeLostEvent(event_proto.lost_event(), event);
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
      return DeserializeThrottleEvent(event_proto.throttle_event(), event);
    case PERF_RECORD_READ:
      return DeserializeReadEvent(event_proto.read_event(), event);
    case PERF_RECORD_AUX:
      return DeserializeAuxEvent(event_proto.aux_event(), event);
    case PERF_RECORD_ITRACE_START:
    case PERF_RECORD_LOST_SAMPLES:
    case PERF_RECORD_SWITCH:
    case PERF_RECORD_SWITCH_CPU_WIDE:
    case PERF_RECORD_NAMESPACES:
      LOG(ERROR) << "Event type: " << event_proto.header().type()
                 << ". Not yet supported.";
      return true;
      break;
  }
  return false;
}

bool PerfSerializer::DeserializeUserEvent(
    const PerfDataProto_PerfEvent& event_proto, event_t* event) const {
  switch (event_proto.header().type()) {
    case PERF_RECORD_AUXTRACE:
      return DeserializeAuxtraceEvent(event_proto.auxtrace_event(), event);
    default:
      // User type events are marked as deserialized because they don't
      // have non-header data in perf.data proto.
      if (event_proto.header().type() >= PERF_RECORD_HEADER_MAX) {
        return false;
      }
  }
  return true;
}

bool PerfSerializer::SerializeEventHeader(
    const perf_event_header& header,
    PerfDataProto_EventHeader* header_proto) const {
  header_proto->set_type(header.type);
  header_proto->set_misc(header.misc);
  header_proto->set_size(header.size);
  return true;
}

bool PerfSerializer::DeserializeEventHeader(
    const PerfDataProto_EventHeader& header_proto,
    perf_event_header* header) const {
  header->type = header_proto.type();
  header->misc = header_proto.misc();
  header->size = header_proto.size();
  return true;
}

bool PerfSerializer::SerializeSampleEvent(
    const event_t& event, PerfDataProto_SampleEvent* sample) const {
  perf_sample sample_info;
  uint64_t sample_type = 0;
  if (!ReadPerfSampleInfoAndType(event, &sample_info, &sample_type))
    return false;

  if (sample_type & PERF_SAMPLE_IP) sample->set_ip(sample_info.ip);
  if (sample_type & PERF_SAMPLE_TID) {
    sample->set_pid(sample_info.pid);
    sample->set_tid(sample_info.tid);
  }
  if (sample_type & PERF_SAMPLE_TIME)
    sample->set_sample_time_ns(sample_info.time);
  if (sample_type & PERF_SAMPLE_ADDR) sample->set_addr(sample_info.addr);
  if ((sample_type & PERF_SAMPLE_ID) || (sample_type & PERF_SAMPLE_IDENTIFIER))
    sample->set_id(sample_info.id);
  if (sample_type & PERF_SAMPLE_STREAM_ID)
    sample->set_stream_id(sample_info.stream_id);
  if (sample_type & PERF_SAMPLE_CPU) sample->set_cpu(sample_info.cpu);
  if (sample_type & PERF_SAMPLE_PERIOD) sample->set_period(sample_info.period);
  if (sample_type & PERF_SAMPLE_RAW) sample->set_raw_size(sample_info.raw_size);
  if (sample_type & PERF_SAMPLE_READ) {
    const SampleInfoReader* reader = GetSampleInfoReaderForEvent(event);
    if (reader) {
      PerfDataProto_ReadInfo* read_info = sample->mutable_read_info();
      if (reader->event_attr().read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        read_info->set_time_enabled(sample_info.read.time_enabled);
      if (reader->event_attr().read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        read_info->set_time_running(sample_info.read.time_running);
      if (reader->event_attr().read_format & PERF_FORMAT_GROUP) {
        for (size_t i = 0; i < sample_info.read.group.nr; i++) {
          auto read_value = read_info->add_read_value();
          read_value->set_value(sample_info.read.group.values[i].value);
          read_value->set_id(sample_info.read.group.values[i].id);
        }
      } else {
        auto read_value = read_info->add_read_value();
        read_value->set_value(sample_info.read.one.value);
        read_value->set_id(sample_info.read.one.id);
      }
    }
  }
  if (sample_type & PERF_SAMPLE_CALLCHAIN) {
    sample->mutable_callchain()->Reserve(sample_info.callchain->nr);
    for (size_t i = 0; i < sample_info.callchain->nr; ++i)
      sample->add_callchain(sample_info.callchain->ips[i]);
  }
  if (sample_type & PERF_SAMPLE_BRANCH_STACK) {
    for (size_t i = 0; i < sample_info.branch_stack->nr; ++i) {
      sample->add_branch_stack();
      const struct branch_entry& entry = sample_info.branch_stack->entries[i];
      sample->mutable_branch_stack(i)->set_from_ip(entry.from);
      sample->mutable_branch_stack(i)->set_to_ip(entry.to);
      sample->mutable_branch_stack(i)->set_mispredicted(entry.flags.mispred);
    }
  }

  if (sample_type & PERF_SAMPLE_WEIGHT) sample->set_weight(sample_info.weight);
  if (sample_type & PERF_SAMPLE_DATA_SRC)
    sample->set_data_src(sample_info.data_src);
  if (sample_type & PERF_SAMPLE_TRANSACTION)
    sample->set_transaction(sample_info.transaction);

  return true;
}

bool PerfSerializer::DeserializeSampleEvent(
    const PerfDataProto_SampleEvent& sample, event_t* event) const {
  perf_sample sample_info;
  if (sample.has_ip()) sample_info.ip = sample.ip();
  if (sample.has_pid()) {
    CHECK(sample.has_tid()) << "Cannot have PID without TID.";
    sample_info.pid = sample.pid();
    sample_info.tid = sample.tid();
  }
  if (sample.has_sample_time_ns()) sample_info.time = sample.sample_time_ns();
  if (sample.has_addr()) sample_info.addr = sample.addr();
  if (sample.has_id()) sample_info.id = sample.id();
  if (sample.has_stream_id()) sample_info.stream_id = sample.stream_id();
  if (sample.has_cpu()) sample_info.cpu = sample.cpu();
  if (sample.has_period()) sample_info.period = sample.period();
  if (sample.has_read_info()) {
    const SampleInfoReader* reader = GetSampleInfoReaderForEvent(*event);
    if (reader) {
      const PerfDataProto_ReadInfo& read_info = sample.read_info();
      if (reader->event_attr().read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        sample_info.read.time_enabled = read_info.time_enabled();
      if (reader->event_attr().read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        sample_info.read.time_running = read_info.time_running();
      if (reader->event_attr().read_format & PERF_FORMAT_GROUP) {
        sample_info.read.group.nr = read_info.read_value_size();
        sample_info.read.group.values =
            new sample_read_value[read_info.read_value_size()];
        for (size_t i = 0; i < sample_info.read.group.nr; i++) {
          sample_info.read.group.values[i].value =
              read_info.read_value(i).value();
          sample_info.read.group.values[i].id = read_info.read_value(i).id();
        }
      } else if (read_info.read_value_size() == 1) {
        sample_info.read.one.value = read_info.read_value(0).value();
        sample_info.read.one.id = read_info.read_value(0).id();
      } else {
        LOG(ERROR) << "Expected read_value array size of 1 but got "
                   << read_info.read_value_size();
      }
    }
  }
  if (sample.callchain_size() > 0) {
    uint64_t callchain_size = sample.callchain_size();
    sample_info.callchain = reinterpret_cast<struct ip_callchain*>(
        new uint64_t[callchain_size + 1]);
    sample_info.callchain->nr = callchain_size;
    for (size_t i = 0; i < callchain_size; ++i)
      sample_info.callchain->ips[i] = sample.callchain(i);
  }
  if (sample.raw_size() > 0) {
    sample_info.raw_size = sample.raw_size();
    sample_info.raw_data = new uint8_t[sample.raw_size()];
    memset(sample_info.raw_data, 0, sample.raw_size());
  }
  if (sample.branch_stack_size() > 0) {
    uint64_t branch_stack_size = sample.branch_stack_size();
    sample_info.branch_stack = reinterpret_cast<struct branch_stack*>(
        new uint8_t[sizeof(uint64_t) +
                    branch_stack_size * sizeof(struct branch_entry)]);
    sample_info.branch_stack->nr = branch_stack_size;
    for (size_t i = 0; i < branch_stack_size; ++i) {
      struct branch_entry& entry = sample_info.branch_stack->entries[i];
      memset(&entry, 0, sizeof(entry));
      entry.from = sample.branch_stack(i).from_ip();
      entry.to = sample.branch_stack(i).to_ip();
      entry.flags.mispred = sample.branch_stack(i).mispredicted();
      entry.flags.predicted = !entry.flags.mispred;
    }
  }

  if (sample.has_weight()) sample_info.weight = sample.weight();
  if (sample.has_data_src()) sample_info.data_src = sample.data_src();
  if (sample.has_transaction()) sample_info.transaction = sample.transaction();

  const SampleInfoReader* writer = GetSampleInfoReaderForId(sample.id());
  CHECK(writer);
  return writer->WritePerfSampleInfo(sample_info, event);
}

bool PerfSerializer::SerializeMMapEvent(const event_t& event,
                                        PerfDataProto_MMapEvent* sample) const {
  const struct mmap_event& mmap = event.mmap;
  sample->set_pid(mmap.pid);
  sample->set_tid(mmap.tid);
  sample->set_start(mmap.start);
  sample->set_len(mmap.len);
  sample->set_pgoff(mmap.pgoff);
  sample->set_filename(mmap.filename);
  sample->set_filename_md5_prefix(Md5Prefix(mmap.filename));

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeMMapEvent(const PerfDataProto_MMapEvent& sample,
                                          event_t* event) const {
  struct mmap_event& mmap = event->mmap;
  mmap.pid = sample.pid();
  mmap.tid = sample.tid();
  mmap.start = sample.start();
  mmap.len = sample.len();
  mmap.pgoff = sample.pgoff();
  snprintf(mmap.filename, PATH_MAX, "%s", sample.filename().c_str());

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeMMap2Event(
    const event_t& event, PerfDataProto_MMapEvent* sample) const {
  const struct mmap2_event& mmap = event.mmap2;
  sample->set_pid(mmap.pid);
  sample->set_tid(mmap.tid);
  sample->set_start(mmap.start);
  sample->set_len(mmap.len);
  sample->set_pgoff(mmap.pgoff);
  sample->set_maj(mmap.maj);
  sample->set_min(mmap.min);
  sample->set_ino(mmap.ino);
  sample->set_ino_generation(mmap.ino_generation);
  sample->set_prot(mmap.prot);
  sample->set_flags(mmap.flags);
  sample->set_filename(mmap.filename);
  sample->set_filename_md5_prefix(Md5Prefix(mmap.filename));

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeMMap2Event(
    const PerfDataProto_MMapEvent& sample, event_t* event) const {
  struct mmap2_event& mmap = event->mmap2;
  mmap.pid = sample.pid();
  mmap.tid = sample.tid();
  mmap.start = sample.start();
  mmap.len = sample.len();
  mmap.pgoff = sample.pgoff();
  mmap.maj = sample.maj();
  mmap.min = sample.min();
  mmap.ino = sample.ino();
  mmap.ino_generation = sample.ino_generation();
  mmap.prot = sample.prot();
  mmap.flags = sample.flags();
  snprintf(mmap.filename, PATH_MAX, "%s", sample.filename().c_str());

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeCommEvent(const event_t& event,
                                        PerfDataProto_CommEvent* sample) const {
  const struct comm_event& comm = event.comm;
  sample->set_pid(comm.pid);
  sample->set_tid(comm.tid);
  sample->set_comm(comm.comm);
  sample->set_comm_md5_prefix(Md5Prefix(comm.comm));

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeCommEvent(const PerfDataProto_CommEvent& sample,
                                          event_t* event) const {
  struct comm_event& comm = event->comm;
  comm.pid = sample.pid();
  comm.tid = sample.tid();
  snprintf(comm.comm, sizeof(comm.comm), "%s", sample.comm().c_str());

  // Sometimes the command string will be modified.  e.g. if the original comm
  // string is not recoverable from the Md5sum prefix, then use the latter as a
  // replacement comm string.  However, if the original was < 8 bytes (fit into
  // |sizeof(uint64_t)|), then the size is no longer correct.  This section
  // checks for the size difference and updates the size in the header.
  const SampleInfoReader* reader =
      GetSampleInfoReaderForId(sample.sample_info().id());
  CHECK(reader);
  uint64_t sample_fields = SampleInfoReader::GetSampleFieldsForEventType(
      comm.header.type, reader->event_attr().sample_type);
  comm.header.size = SampleInfoReader::GetPerfSampleDataOffset(*event) +
                     GetNumBits(sample_fields) * sizeof(uint64_t);

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeForkExitEvent(
    const event_t& event, PerfDataProto_ForkEvent* sample) const {
  const struct fork_event& fork = event.fork;
  sample->set_pid(fork.pid);
  sample->set_ppid(fork.ppid);
  sample->set_tid(fork.tid);
  sample->set_ptid(fork.ptid);
  sample->set_fork_time_ns(fork.time);

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeForkExitEvent(
    const PerfDataProto_ForkEvent& sample, event_t* event) const {
  struct fork_event& fork = event->fork;
  fork.pid = sample.pid();
  fork.ppid = sample.ppid();
  fork.tid = sample.tid();
  fork.ptid = sample.ptid();
  fork.time = sample.fork_time_ns();

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeLostEvent(const event_t& event,
                                        PerfDataProto_LostEvent* sample) const {
  const struct lost_event& lost = event.lost;
  sample->set_id(lost.id);
  sample->set_lost(lost.lost);

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeLostEvent(const PerfDataProto_LostEvent& sample,
                                          event_t* event) const {
  struct lost_event& lost = event->lost;
  lost.id = sample.id();
  lost.lost = sample.lost();

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeThrottleEvent(
    const event_t& event, PerfDataProto_ThrottleEvent* sample) const {
  const struct throttle_event& throttle = event.throttle;
  sample->set_time_ns(throttle.time);
  sample->set_id(throttle.id);
  sample->set_stream_id(throttle.stream_id);

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeThrottleEvent(
    const PerfDataProto_ThrottleEvent& sample, event_t* event) const {
  struct throttle_event& throttle = event->throttle;
  throttle.time = sample.time_ns();
  throttle.id = sample.id();
  throttle.stream_id = sample.stream_id();

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeReadEvent(const event_t& event,
                                        PerfDataProto_ReadEvent* sample) const {
  const struct read_event& read = event.read;
  sample->set_pid(read.pid);
  sample->set_tid(read.tid);
  sample->set_value(read.value);
  sample->set_time_enabled(read.time_enabled);
  sample->set_time_running(read.time_running);
  sample->set_id(read.id);

  return true;
}

bool PerfSerializer::DeserializeReadEvent(const PerfDataProto_ReadEvent& sample,
                                          event_t* event) const {
  struct read_event& read = event->read;
  read.pid = sample.pid();
  read.tid = sample.tid();
  read.value = sample.value();
  read.time_enabled = sample.time_enabled();
  read.time_running = sample.time_running();
  read.id = sample.id();

  return true;
}

bool PerfSerializer::SerializeAuxEvent(const event_t& event,
                                       PerfDataProto_AuxEvent* sample) const {
  const struct aux_event& aux = event.aux;
  sample->set_aux_offset(aux.aux_offset);
  sample->set_aux_size(aux.aux_size);
  sample->set_is_truncated(aux.flags & PERF_AUX_FLAG_TRUNCATED ? true : false);
  sample->set_is_overwrite(aux.flags & PERF_AUX_FLAG_OVERWRITE ? true : false);
  sample->set_is_partial(aux.flags & PERF_AUX_FLAG_PARTIAL ? true : false);
  if (aux.flags & ~(PERF_AUX_FLAG_TRUNCATED | PERF_AUX_FLAG_OVERWRITE |
                    PERF_AUX_FLAG_PARTIAL)) {
    LOG(WARNING) << "Ignoring unknown PERF_RECORD_AUX flag: " << aux.flags;
  }

  return SerializeSampleInfo(event, sample->mutable_sample_info());
}

bool PerfSerializer::DeserializeAuxEvent(const PerfDataProto_AuxEvent& sample,
                                         event_t* event) const {
  struct aux_event& aux = event->aux;
  aux.aux_offset = sample.aux_offset();
  aux.aux_size = sample.aux_size();
  aux.flags |= sample.is_truncated() ? PERF_AUX_FLAG_TRUNCATED : 0;
  aux.flags |= sample.is_overwrite() ? PERF_AUX_FLAG_OVERWRITE : 0;
  aux.flags |= sample.is_partial() ? PERF_AUX_FLAG_PARTIAL : 0;

  return DeserializeSampleInfo(sample.sample_info(), event);
}

bool PerfSerializer::SerializeSampleInfo(
    const event_t& event, PerfDataProto_SampleInfo* sample) const {
  if (!SampleIdAll()) return true;

  perf_sample sample_info;
  uint64_t sample_type = 0;
  if (!ReadPerfSampleInfoAndType(event, &sample_info, &sample_type))
    return false;

  if (sample_type & PERF_SAMPLE_TID) {
    sample->set_pid(sample_info.pid);
    sample->set_tid(sample_info.tid);
  }
  if (sample_type & PERF_SAMPLE_TIME)
    sample->set_sample_time_ns(sample_info.time);
  if ((sample_type & PERF_SAMPLE_ID) || (sample_type & PERF_SAMPLE_IDENTIFIER))
    sample->set_id(sample_info.id);
  if (sample_type & PERF_SAMPLE_CPU) sample->set_cpu(sample_info.cpu);
  if (sample_type & PERF_SAMPLE_STREAM_ID)
    sample->set_stream_id(sample_info.stream_id);
  return true;
}

bool PerfSerializer::DeserializeSampleInfo(
    const PerfDataProto_SampleInfo& sample, event_t* event) const {
  if (!SampleIdAll()) return true;

  perf_sample sample_info;
  if (sample.has_tid()) {
    sample_info.pid = sample.pid();
    sample_info.tid = sample.tid();
  }
  if (sample.has_sample_time_ns()) sample_info.time = sample.sample_time_ns();
  if (sample.has_id()) sample_info.id = sample.id();
  if (sample.has_cpu()) sample_info.cpu = sample.cpu();
  if (sample.has_stream_id()) sample_info.stream_id = sample.stream_id();

  const SampleInfoReader* writer = GetSampleInfoReaderForId(sample.id());
  CHECK(writer);
  return writer->WritePerfSampleInfo(sample_info, event);
}

bool PerfSerializer::SerializeTracingMetadata(const std::vector<char>& from,
                                              PerfDataProto* to) const {
  if (from.empty()) {
    return true;
  }
  PerfDataProto_PerfTracingMetadata* data = to->mutable_tracing_data();
  data->set_tracing_data(from.data(), from.size());
  data->set_tracing_data_md5_prefix(Md5Prefix(from));

  return true;
}

bool PerfSerializer::DeserializeTracingMetadata(const PerfDataProto& from,
                                                std::vector<char>* to) const {
  if (!from.has_tracing_data()) {
    to->clear();
    return true;
  }

  const PerfDataProto_PerfTracingMetadata& data = from.tracing_data();
  to->assign(data.tracing_data().begin(), data.tracing_data().end());
  return true;
}

bool PerfSerializer::SerializeBuildIDEvent(
    const malloced_unique_ptr<build_id_event>& from,
    PerfDataProto_PerfBuildID* to) const {
  to->set_misc(from->header.misc);
  to->set_pid(from->pid);
  to->set_filename(from->filename);
  to->set_filename_md5_prefix(Md5Prefix(from->filename));

  // Trim out trailing zeroes from the build ID.
  string build_id = RawDataToHexString(from->build_id, kBuildIDArraySize);
  TrimZeroesFromBuildIDString(&build_id);

  uint8_t build_id_bytes[kBuildIDArraySize];
  if (!HexStringToRawData(build_id, build_id_bytes, sizeof(build_id_bytes)))
    return false;

  // Used to convert build IDs (and possibly other hashes) between raw data
  // format and as string of hex digits.
  const int kHexCharsPerByte = 2;
  to->set_build_id_hash(build_id_bytes, build_id.size() / kHexCharsPerByte);

  return true;
}

bool PerfSerializer::DeserializeBuildIDEvent(
    const PerfDataProto_PerfBuildID& from,
    malloced_unique_ptr<build_id_event>* to) const {
  const string& filename = from.filename();
  size_t size = sizeof(build_id_event) + GetUint64AlignedStringLength(filename);

  malloced_unique_ptr<build_id_event>& event = *to;
  event.reset(CallocMemoryForBuildID(size));
  event->header.type = PERF_RECORD_HEADER_BUILD_ID;
  event->header.size = size;
  event->header.misc = from.misc();
  event->pid = from.pid();
  memcpy(event->build_id, from.build_id_hash().c_str(),
         from.build_id_hash().size());

  if (from.has_filename() && !filename.empty()) {
    CHECK_GT(
        snprintf(event->filename, filename.size() + 1, "%s", filename.c_str()),
        0);
  }
  return true;
}

bool PerfSerializer::SerializeAuxtraceEvent(
    const event_t& event, PerfDataProto_AuxtraceEvent* sample) const {
  const struct auxtrace_event& auxtrace = event.auxtrace;
  sample->set_size(auxtrace.size);
  sample->set_offset(auxtrace.offset);
  sample->set_reference(auxtrace.reference);
  sample->set_idx(auxtrace.idx);
  sample->set_tid(auxtrace.tid);
  sample->set_cpu(auxtrace.cpu);

  return true;
}

bool PerfSerializer::SerializeAuxtraceEventTraceData(
    const std::vector<char>& from, PerfDataProto_AuxtraceEvent* to) const {
  if (from.empty()) {
    return true;
  }
  to->set_trace_data(from.data(), from.size());

  return true;
}

bool PerfSerializer::DeserializeAuxtraceEvent(
    const PerfDataProto_AuxtraceEvent& sample, event_t* event) const {
  struct auxtrace_event& auxtrace = event->auxtrace;
  auxtrace.size = sample.size();
  auxtrace.offset = sample.offset();
  auxtrace.reference = sample.reference();
  auxtrace.idx = sample.idx();
  auxtrace.tid = sample.tid();
  auxtrace.cpu = sample.cpu();

  return true;
}

bool PerfSerializer::DeserializeAuxtraceEventTraceData(
    const PerfDataProto_AuxtraceEvent& from, std::vector<char>* to) const {
  to->assign(from.trace_data().begin(), from.trace_data().end());
  return true;
}

bool PerfSerializer::SerializeSingleUint32Metadata(
    const PerfUint32Metadata& metadata,
    PerfDataProto_PerfUint32Metadata* proto_metadata) const {
  proto_metadata->set_type(metadata.type);
  for (size_t i = 0; i < metadata.data.size(); ++i)
    proto_metadata->add_data(metadata.data[i]);
  return true;
}

bool PerfSerializer::DeserializeSingleUint32Metadata(
    const PerfDataProto_PerfUint32Metadata& proto_metadata,
    PerfUint32Metadata* metadata) const {
  metadata->type = proto_metadata.type();
  for (int i = 0; i < proto_metadata.data_size(); ++i)
    metadata->data.push_back(proto_metadata.data(i));
  return true;
}

bool PerfSerializer::SerializeSingleUint64Metadata(
    const PerfUint64Metadata& metadata,
    PerfDataProto_PerfUint64Metadata* proto_metadata) const {
  proto_metadata->set_type(metadata.type);
  for (size_t i = 0; i < metadata.data.size(); ++i)
    proto_metadata->add_data(metadata.data[i]);
  return true;
}

bool PerfSerializer::DeserializeSingleUint64Metadata(
    const PerfDataProto_PerfUint64Metadata& proto_metadata,
    PerfUint64Metadata* metadata) const {
  metadata->type = proto_metadata.type();
  for (int i = 0; i < proto_metadata.data_size(); ++i)
    metadata->data.push_back(proto_metadata.data(i));
  return true;
}

bool PerfSerializer::SerializeCPUTopologyMetadata(
    const PerfCPUTopologyMetadata& metadata,
    PerfDataProto_PerfCPUTopologyMetadata* proto_metadata) const {
  for (const string& core_name : metadata.core_siblings) {
    proto_metadata->add_core_siblings(core_name);
    proto_metadata->add_core_siblings_md5_prefix(Md5Prefix(core_name));
  }
  for (const string& thread_name : metadata.thread_siblings) {
    proto_metadata->add_thread_siblings(thread_name);
    proto_metadata->add_thread_siblings_md5_prefix(Md5Prefix(thread_name));
  }
  return true;
}

bool PerfSerializer::DeserializeCPUTopologyMetadata(
    const PerfDataProto_PerfCPUTopologyMetadata& proto_metadata,
    PerfCPUTopologyMetadata* metadata) const {
  metadata->core_siblings.clear();
  metadata->core_siblings.reserve(proto_metadata.core_siblings().size());
  std::copy(proto_metadata.core_siblings().begin(),
            proto_metadata.core_siblings().end(),
            std::back_inserter(metadata->core_siblings));

  metadata->thread_siblings.clear();
  metadata->thread_siblings.reserve(proto_metadata.thread_siblings().size());
  std::copy(proto_metadata.thread_siblings().begin(),
            proto_metadata.thread_siblings().end(),
            std::back_inserter(metadata->thread_siblings));
  return true;
}

bool PerfSerializer::SerializeNodeTopologyMetadata(
    const PerfNodeTopologyMetadata& metadata,
    PerfDataProto_PerfNodeTopologyMetadata* proto_metadata) const {
  proto_metadata->set_id(metadata.id);
  proto_metadata->set_total_memory(metadata.total_memory);
  proto_metadata->set_free_memory(metadata.free_memory);
  proto_metadata->set_cpu_list(metadata.cpu_list);
  proto_metadata->set_cpu_list_md5_prefix(Md5Prefix(metadata.cpu_list));
  return true;
}

bool PerfSerializer::DeserializeNodeTopologyMetadata(
    const PerfDataProto_PerfNodeTopologyMetadata& proto_metadata,
    PerfNodeTopologyMetadata* metadata) const {
  metadata->id = proto_metadata.id();
  metadata->total_memory = proto_metadata.total_memory();
  metadata->free_memory = proto_metadata.free_memory();
  metadata->cpu_list = proto_metadata.cpu_list();
  return true;
}

bool PerfSerializer::SerializePMUMappingsMetadata(
    const PerfPMUMappingsMetadata& metadata,
    PerfDataProto_PerfPMUMappingsMetadata* proto_metadata) const {
  proto_metadata->set_type(metadata.type);
  proto_metadata->set_name(metadata.name);
  proto_metadata->set_name_md5_prefix(Md5Prefix(metadata.name));
  return true;
}

bool PerfSerializer::DeserializePMUMappingsMetadata(
    const PerfDataProto_PerfPMUMappingsMetadata& proto_metadata,
    PerfPMUMappingsMetadata* metadata) const {
  metadata->type = proto_metadata.type();
  metadata->name = proto_metadata.name();
  return true;
}

bool PerfSerializer::SerializeGroupDescMetadata(
    const PerfGroupDescMetadata& metadata,
    PerfDataProto_PerfGroupDescMetadata* proto_metadata) const {
  proto_metadata->set_name(metadata.name);
  proto_metadata->set_name_md5_prefix(Md5Prefix(metadata.name));
  proto_metadata->set_leader_idx(metadata.leader_idx);
  proto_metadata->set_num_members(metadata.num_members);
  return true;
}

bool PerfSerializer::DeserializeGroupDescMetadata(
    const PerfDataProto_PerfGroupDescMetadata& proto_metadata,
    PerfGroupDescMetadata* metadata) const {
  metadata->name = proto_metadata.name();
  metadata->leader_idx = proto_metadata.leader_idx();
  metadata->num_members = proto_metadata.num_members();
  return true;
}

// static
void PerfSerializer::SerializeParserStats(const PerfEventStats& stats,
                                          PerfDataProto* perf_data_proto) {
  PerfDataProto_PerfEventStats* stats_pb = perf_data_proto->mutable_stats();
  stats_pb->set_num_sample_events(stats.num_sample_events);
  stats_pb->set_num_mmap_events(stats.num_mmap_events);
  stats_pb->set_num_fork_events(stats.num_fork_events);
  stats_pb->set_num_exit_events(stats.num_exit_events);
  stats_pb->set_did_remap(stats.did_remap);
  stats_pb->set_num_sample_events_mapped(stats.num_sample_events_mapped);
}

// static
void PerfSerializer::DeserializeParserStats(
    const PerfDataProto& perf_data_proto, PerfEventStats* stats) {
  const PerfDataProto_PerfEventStats& stats_pb = perf_data_proto.stats();
  stats->num_sample_events = stats_pb.num_sample_events();
  stats->num_mmap_events = stats_pb.num_mmap_events();
  stats->num_fork_events = stats_pb.num_fork_events();
  stats->num_exit_events = stats_pb.num_exit_events();
  stats->did_remap = stats_pb.did_remap();
  stats->num_sample_events_mapped = stats_pb.num_sample_events_mapped();
}

void PerfSerializer::CreateSampleInfoReader(const PerfFileAttr& attr,
                                            bool read_cross_endian) {
  for (const auto& id :
       (attr.ids.empty() ? std::initializer_list<u64>({0}) : attr.ids)) {
    sample_info_reader_map_[id].reset(
        new SampleInfoReader(attr.attr, read_cross_endian));
  }
  UpdateEventIdPositions(attr.attr);
}

void PerfSerializer::UpdateEventIdPositions(
    const struct perf_event_attr& attr) {
  const u64 sample_type = attr.sample_type;
  ssize_t new_sample_event_id_pos = EventIdPosition::NotPresent;
  ssize_t new_other_event_id_pos = EventIdPosition::NotPresent;
  if (sample_type & PERF_SAMPLE_IDENTIFIER) {
    new_sample_event_id_pos = 0;
    new_other_event_id_pos = 1;
  } else if (sample_type & PERF_SAMPLE_ID) {
    // Increment for IP, TID, TIME, ADDR
    new_sample_event_id_pos = 0;
    if (sample_type & PERF_SAMPLE_IP) new_sample_event_id_pos++;
    if (sample_type & PERF_SAMPLE_TID) new_sample_event_id_pos++;
    if (sample_type & PERF_SAMPLE_TIME) new_sample_event_id_pos++;
    if (sample_type & PERF_SAMPLE_ADDR) new_sample_event_id_pos++;

    // Increment for CPU, STREAM_ID
    new_other_event_id_pos = 1;
    if (sample_type & PERF_SAMPLE_CPU) new_other_event_id_pos++;
    if (sample_type & PERF_SAMPLE_STREAM_ID) new_other_event_id_pos++;
  }

  if (sample_event_id_pos_ == EventIdPosition::Uninitialized) {
    sample_event_id_pos_ = new_sample_event_id_pos;
  } else {
    CHECK_EQ(new_sample_event_id_pos, sample_event_id_pos_)
        << "Event ids must be in a consistent positition";
  }
  if (other_event_id_pos_ == EventIdPosition::Uninitialized) {
    other_event_id_pos_ = new_other_event_id_pos;
  } else {
    CHECK_EQ(new_other_event_id_pos, other_event_id_pos_)
        << "Event ids must be in a consistent positition";
  }
}

bool PerfSerializer::SampleIdAll() const {
  if (sample_info_reader_map_.empty()) {
    return false;
  }
  return sample_info_reader_map_.begin()->second->event_attr().sample_id_all;
}

const SampleInfoReader* PerfSerializer::GetSampleInfoReaderForEvent(
    const event_t& event) const {
  // Where is the event id?
  ssize_t event_id_pos = EventIdPosition::Uninitialized;
  if (event.header.type == PERF_RECORD_SAMPLE) {
    event_id_pos = sample_event_id_pos_;
  } else if (SampleIdAll()) {
    event_id_pos = other_event_id_pos_;
  } else {
    event_id_pos = EventIdPosition::NotPresent;
  }

  // What is the event id?
  u64 event_id;
  switch (event_id_pos) {
    case EventIdPosition::Uninitialized:
      LOG(FATAL) << "Position of the event id was not initialized!";
      return nullptr;
    case EventIdPosition::NotPresent:
      event_id = 0;
      break;
    default:
      if (event.header.type == PERF_RECORD_SAMPLE) {
        event_id = event.sample.array[event_id_pos];
      } else {
        // Pretend this is a sample event--ie, an array of u64. Find the length
        // of the array. The sample id is at the end of the array, and
        // event_id_pos (aka other_event_id_pos_) counts from the end.
        size_t event_end_pos =
            (event.header.size - sizeof(event.header)) / sizeof(u64);
        event_id = event.sample.array[event_end_pos - event_id_pos];
      }
      break;
  }
  return GetSampleInfoReaderForId(event_id);
}

const SampleInfoReader* PerfSerializer::GetSampleInfoReaderForId(
    uint64_t id) const {
  if (id) {
    auto iter = sample_info_reader_map_.find(id);
    if (iter == sample_info_reader_map_.end()) return nullptr;
    return iter->second.get();
  }

  if (sample_info_reader_map_.empty()) return nullptr;
  return sample_info_reader_map_.begin()->second.get();
}

bool PerfSerializer::ReadPerfSampleInfoAndType(const event_t& event,
                                               perf_sample* sample_info,
                                               uint64_t* sample_type) const {
  const SampleInfoReader* reader = GetSampleInfoReaderForEvent(event);
  if (!reader) {
    LOG(ERROR) << "No SampleInfoReader available";
    return false;
  }

  if (!reader->ReadPerfSampleInfo(event, sample_info)) return false;
  *sample_type = reader->event_attr().sample_type;
  return true;
}

}  // namespace quipper
