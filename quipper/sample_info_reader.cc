// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sample_info_reader.h"

#include <string.h>

#include "base/logging.h"
#include "buffer_reader.h"
#include "buffer_writer.h"
#include "kernel/perf_internals.h"
#include "perf_data_utils.h"

namespace quipper {

namespace {

bool IsSupportedEventType(uint32_t type) {
  switch (type) {
  case PERF_RECORD_SAMPLE:
  case PERF_RECORD_MMAP:
  case PERF_RECORD_MMAP2:
  case PERF_RECORD_FORK:
  case PERF_RECORD_EXIT:
  case PERF_RECORD_COMM:
  case PERF_RECORD_LOST:
  case PERF_RECORD_THROTTLE:
  case PERF_RECORD_UNTHROTTLE:
    return true;
  case PERF_RECORD_READ:
  case PERF_RECORD_MAX:
    return false;
  default:
    LOG(FATAL) << "Unknown event type " << type;
    return false;
  }
}

// Read read info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_READ in the !PERF_FORMAT_GROUP case.
void ReadReadInfo(DataReader* reader,
                  uint64_t read_format,
                  struct perf_sample* sample) {
  reader->ReadUint64(&sample->read.one.value);
  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
    reader->ReadUint64(&sample->read.time_enabled);
  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
    reader->ReadUint64(&sample->read.time_running);
  if (read_format & PERF_FORMAT_ID)
    reader->ReadUint64(&sample->read.one.id);
}

// Read read info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_READ in the PERF_FORMAT_GROUP case.
void ReadGroupReadInfo(DataReader* reader,
                       uint64_t read_format,
                       struct perf_sample* sample) {
  uint64_t num = 0;
  reader->ReadUint64(&num);
  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
    reader->ReadUint64(&sample->read.time_enabled);
  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
    reader->ReadUint64(&sample->read.time_running);

  // Make sure there is no existing allocated memory in
  // |sample->read.group.values|.
  CHECK_EQ(static_cast<void*>(NULL), sample->read.group.values);
  sample_read_value* values = new sample_read_value[num];
  for (uint64_t i = 0; i < num; i++) {
    reader->ReadUint64(&values[i].value);
    if (read_format & PERF_FORMAT_ID)
      reader->ReadUint64(&values[i].id);
  }
  sample->read.group.nr = num;
  sample->read.group.values = values;
}

// Read call chain info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_CALLCHAIN.
void ReadCallchain(DataReader* reader, struct perf_sample* sample) {
  // Make sure there is no existing allocated memory in |sample->callchain|.
  CHECK_EQ(static_cast<void*>(NULL), sample->callchain);

  // The callgraph data consists of a uint64_t value |nr| followed by |nr|
  // addresses.
  uint64_t callchain_size = 0;
  reader->ReadUint64(&callchain_size);

  struct ip_callchain* callchain =
      reinterpret_cast<struct ip_callchain*>(new uint64_t[callchain_size + 1]);
  callchain->nr = callchain_size;

  for (size_t i = 0; i < callchain_size; ++i)
    reader->ReadUint64(&callchain->ips[i]);

  sample->callchain = callchain;
}

// Read raw info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_RAW.
void ReadRawData(DataReader* reader, struct perf_sample* sample) {
  // Save the original read offset.
  size_t reader_offset = reader->Tell();

  reader->ReadUint32(&sample->raw_size);

  // Allocate space for and read the raw data bytes.
  sample->raw_data = new uint8_t[sample->raw_size];
  reader->ReadData(sample->raw_size, sample->raw_data);

  // Determine the bytes that were read, and align to the next 64 bits.
  reader_offset += Align<uint64_t>(sizeof(sample->raw_size) + sample->raw_size);
  reader->SeekSet(reader_offset);
}

// Read call chain info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_CALLCHAIN.
void ReadBranchStack(DataReader* reader, struct perf_sample* sample) {
  // Make sure there is no existing allocated memory in
  // |sample->branch_stack|.
  CHECK_EQ(static_cast<void*>(NULL), sample->branch_stack);

  // The branch stack data consists of a uint64_t value |nr| followed by |nr|
  // branch_entry structs.
  uint64_t branch_stack_size = 0;
  reader->ReadUint64(&branch_stack_size);

  struct branch_stack* branch_stack =
      reinterpret_cast<struct branch_stack*>(
          new uint8_t[sizeof(uint64_t) +
                      branch_stack_size * sizeof(struct branch_entry)]);
  branch_stack->nr = branch_stack_size;
  for (size_t i = 0; i < branch_stack_size; ++i) {
    reader->ReadUint64(&branch_stack->entries[i].from);
    reader->ReadUint64(&branch_stack->entries[i].to);
    reader->ReadData(sizeof(branch_stack->entries[i].flags),
                     &branch_stack->entries[i].flags);
    if (reader->is_cross_endian()) {
      LOG(ERROR) << "Byte swapping of branch stack flags is not yet supported.";
    }
  }
  sample->branch_stack = branch_stack;
}

// Reads perf sample info data from |event| into |sample|.
// |attr| is the event attribute struct, which contains info such as which
// sample info fields are present.
// |is_cross_endian| indicates that the data is cross-endian and that the byte
// order should be reversed for each field according to its size.
// Returns number of bytes of data read or skipped.
size_t ReadPerfSampleFromData(const event_t& event,
                              const struct perf_event_attr& attr,
                              bool is_cross_endian,
                              struct perf_sample* sample) {
  BufferReader reader(&event, event.header.size);
  reader.set_is_cross_endian(is_cross_endian);
  reader.SeekSet(SampleInfoReader::GetPerfSampleDataOffset(event));

  if (!(event.header.type == PERF_RECORD_SAMPLE || attr.sample_id_all)) {
    return reader.Tell();
  }

  uint64_t sample_fields =
      SampleInfoReader::GetSampleFieldsForEventType(event.header.type,
                                                    attr.sample_type);

  // See structure for PERF_RECORD_SAMPLE in kernel/perf_event.h
  // and compare sample_id when sample_id_all is set.

  // NB: For sample_id, sample_fields has already been masked to the set
  // of fields in that struct by GetSampleFieldsForEventType. That set
  // of fields is mostly in the same order as PERF_RECORD_SAMPLE, with
  // the exception of PERF_SAMPLE_IDENTIFIER.

  // PERF_SAMPLE_IDENTIFIER is in a different location depending on
  // if this is a SAMPLE event or the sample_id of another event.
  if (event.header.type == PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER) {
      reader.ReadUint64(&sample->id);
    }
  }

  // { u64                   ip;       } && PERF_SAMPLE_IP
  if (sample_fields & PERF_SAMPLE_IP) {
    reader.ReadUint64(&sample->ip);
  }

  // { u32                   pid, tid; } && PERF_SAMPLE_TID
  if (sample_fields & PERF_SAMPLE_TID) {
    reader.ReadUint32(&sample->pid);
    reader.ReadUint32(&sample->tid);
  }

  // { u64                   time;     } && PERF_SAMPLE_TIME
  if (sample_fields & PERF_SAMPLE_TIME) {
    reader.ReadUint64(&sample->time);
  }

  // { u64                   addr;     } && PERF_SAMPLE_ADDR
  if (sample_fields & PERF_SAMPLE_ADDR) {
    reader.ReadUint64(&sample->addr);
  }

  // { u64                   id;       } && PERF_SAMPLE_ID
  if (sample_fields & PERF_SAMPLE_ID) {
    reader.ReadUint64(&sample->id);
  }

  // { u64                   stream_id;} && PERF_SAMPLE_STREAM_ID
  if (sample_fields & PERF_SAMPLE_STREAM_ID) {
    reader.ReadUint64(&sample->stream_id);
  }

  // { u32                   cpu, res; } && PERF_SAMPLE_CPU
  if (sample_fields & PERF_SAMPLE_CPU) {
    reader.ReadUint32(&sample->cpu);

    // The PERF_SAMPLE_CPU format bit specifies 64-bits of data, but the actual
    // CPU number is really only 32 bits. There is an extra 32-bit word of
    // reserved padding, as the whole field is aligned to 64 bits.

    // reader.ReadUint32(&sample->res);  // reserved
    u32 reserved;
    reader.ReadUint32(&reserved);
  }

  // This is the location of PERF_SAMPLE_IDENTIFIER in struct sample_id.
  if (event.header.type != PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER) {
      reader.ReadUint64(&sample->id);
    }
  }

  //
  // The remaining fields are only in PERF_RECORD_SAMPLE
  //

  // { u64                   period;   } && PERF_SAMPLE_PERIOD
  if (sample_fields & PERF_SAMPLE_PERIOD) {
    reader.ReadUint64(&sample->period);
  }

  // { struct read_format    values;   } && PERF_SAMPLE_READ
  if (sample_fields & PERF_SAMPLE_READ) {
    if (attr.read_format & PERF_FORMAT_GROUP)
      ReadGroupReadInfo(&reader, attr.read_format, sample);
    else
      ReadReadInfo(&reader, attr.read_format, sample);
  }

  // { u64                   nr,
  //   u64                   ips[nr];  } && PERF_SAMPLE_CALLCHAIN
  if (sample_fields & PERF_SAMPLE_CALLCHAIN) {
    ReadCallchain(&reader, sample);
  }

  // { u32                   size;
  //   char                  data[size];}&& PERF_SAMPLE_RAW
  if (sample_fields & PERF_SAMPLE_RAW) {
    ReadRawData(&reader, sample);
  }

  // { u64                   nr;
  //   { u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
  if (sample_fields & PERF_SAMPLE_BRANCH_STACK) {
    ReadBranchStack(&reader, sample);
  }

  // { u64                   abi; # enum perf_sample_regs_abi
  //   u64                   regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
  if (sample_fields & PERF_SAMPLE_REGS_USER) {
    LOG(ERROR) << "PERF_SAMPLE_REGS_USER is not yet supported.";
    return reader.Tell();
  }

  // { u64                   size;
  //   char                  data[size];
  //   u64                   dyn_size; } && PERF_SAMPLE_STACK_USER
  if (sample_fields & PERF_SAMPLE_STACK_USER) {
    LOG(ERROR) << "PERF_SAMPLE_STACK_USER is not yet supported.";
    return reader.Tell();
  }

  // { u64                   weight;   } && PERF_SAMPLE_WEIGHT
  if (sample_fields & PERF_SAMPLE_WEIGHT) {
    reader.ReadUint64(&sample->weight);
  }

  // { u64                   data_src; } && PERF_SAMPLE_DATA_SRC
  if (sample_fields & PERF_SAMPLE_DATA_SRC) {
    reader.ReadUint64(&sample->data_src);
  }

  // { u64                   transaction; } && PERF_SAMPLE_TRANSACTION
  if (sample_fields & PERF_SAMPLE_TRANSACTION) {
    reader.ReadUint64(&sample->transaction);
  }

  if (sample_fields & ~(PERF_SAMPLE_MAX-1)) {
    LOG(WARNING) << "Unrecognized sample fields 0x"
                 << std::hex << (sample_fields & ~(PERF_SAMPLE_MAX-1));
  }

  return reader.Tell();
}

// Writes sample info data data from |sample| into |event|.
// |attr| is the event attribute struct, which contains info such as which
// sample info fields are present.
// |event| is the destination event. Its header should already be filled out.
// Returns the number of bytes written or skipped.
size_t WritePerfSampleToData(const struct perf_sample& sample,
                             const struct perf_event_attr& attr,
                             event_t* event) {
  const uint64_t* initial_array_ptr = reinterpret_cast<const uint64_t*>(event);

  uint64_t offset = SampleInfoReader::GetPerfSampleDataOffset(*event);
  uint64_t* array =
      reinterpret_cast<uint64_t*>(event) + offset / sizeof(uint64_t);

  if (!(event->header.type == PERF_RECORD_SAMPLE || attr.sample_id_all)) {
    return offset;
  }

  uint64_t sample_fields =
      SampleInfoReader::GetSampleFieldsForEventType(event->header.type,
                                                    attr.sample_type);

  union {
    uint32_t val32[sizeof(uint64_t) / sizeof(uint32_t)];
    uint64_t val64;
  };

  // See notes at the top of ReadPerfSampleFromData regarding the structure
  // of PERF_RECORD_SAMPLE, sample_id, and PERF_SAMPLE_IDENTIFIER, as they
  // all apply here as well.

  // PERF_SAMPLE_IDENTIFIER is in a different location depending on
  // if this is a SAMPLE event or the sample_id of another event.
  if (event->header.type == PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER) {
      *array++ = sample.id;
    }
  }

  // { u64                   ip;       } && PERF_SAMPLE_IP
  if (sample_fields & PERF_SAMPLE_IP) {
    *array++ = sample.ip;
  }

  // { u32                   pid, tid; } && PERF_SAMPLE_TID
  if (sample_fields & PERF_SAMPLE_TID) {
    val32[0] = sample.pid;
    val32[1] = sample.tid;
    *array++ = val64;
  }

  // { u64                   time;     } && PERF_SAMPLE_TIME
  if (sample_fields & PERF_SAMPLE_TIME) {
    *array++ = sample.time;
  }

  // { u64                   addr;     } && PERF_SAMPLE_ADDR
  if (sample_fields & PERF_SAMPLE_ADDR) {
    *array++ = sample.addr;
  }

  // { u64                   id;       } && PERF_SAMPLE_ID
  if (sample_fields & PERF_SAMPLE_ID) {
    *array++ = sample.id;
  }

  // { u64                   stream_id;} && PERF_SAMPLE_STREAM_ID
  if (sample_fields & PERF_SAMPLE_STREAM_ID) {
    *array++ = sample.stream_id;
  }

  // { u32                   cpu, res; } && PERF_SAMPLE_CPU
  if (sample_fields & PERF_SAMPLE_CPU) {
    val32[0] = sample.cpu;
    // val32[1] = sample.res;  // reserved
    val32[1] = 0;
    *array++ = val64;
  }

  // This is the location of PERF_SAMPLE_IDENTIFIER in struct sample_id.
  if (event->header.type != PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER) {
      *array++ = sample.id;
    }
  }

  //
  // The remaining fields are only in PERF_RECORD_SAMPLE
  //

  // { u64                   period;   } && PERF_SAMPLE_PERIOD
  if (sample_fields & PERF_SAMPLE_PERIOD) {
    *array++ = sample.period;
  }

  // { struct read_format    values;   } && PERF_SAMPLE_READ
  if (sample_fields & PERF_SAMPLE_READ) {
    if (attr.read_format & PERF_FORMAT_GROUP) {
      *array++ = sample.read.group.nr;
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        *array++ = sample.read.time_enabled;
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        *array++ = sample.read.time_running;
      for (size_t i = 0; i < sample.read.group.nr; i++) {
        *array++ = sample.read.group.values[i].value;
        if (attr.read_format & PERF_FORMAT_ID)
          *array++ = sample.read.one.id;
      }
    } else {
      *array++ = sample.read.one.value;
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        *array++ = sample.read.time_enabled;
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        *array++ = sample.read.time_running;
      if (attr.read_format & PERF_FORMAT_ID)
        *array++ = sample.read.one.id;
    }
  }

  // { u64                   nr,
  //   u64                   ips[nr];  } && PERF_SAMPLE_CALLCHAIN
  if (sample_fields & PERF_SAMPLE_CALLCHAIN) {
    if (!sample.callchain) {
      LOG(ERROR) << "Expecting callchain data, but none was found.";
    } else {
      *array++ = sample.callchain->nr;
      for (size_t i = 0; i < sample.callchain->nr; ++i)
        *array++ = sample.callchain->ips[i];
    }
  }

  // { u32                   size;
  //   char                  data[size];}&& PERF_SAMPLE_RAW
  if (sample_fields & PERF_SAMPLE_RAW) {
    uint32_t* ptr = reinterpret_cast<uint32_t*>(array);
    *ptr++ = sample.raw_size;
    memcpy(ptr, sample.raw_data, sample.raw_size);

    // Update the data read pointer after aligning to the next 64 bytes.
    int num_bytes = Align<uint64_t>(sizeof(sample.raw_size) + sample.raw_size);
    array += num_bytes / sizeof(uint64_t);
  }

  // { u64                   nr;
  //   { u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
  if (sample_fields & PERF_SAMPLE_BRANCH_STACK) {
    if (!sample.branch_stack) {
      LOG(ERROR) << "Expecting branch stack data, but none was found.";
    } else {
      *array++ = sample.branch_stack->nr;
      for (size_t i = 0; i < sample.branch_stack->nr; ++i) {
        *array++ = sample.branch_stack->entries[i].from;
        *array++ = sample.branch_stack->entries[i].to;
        memcpy(array++, &sample.branch_stack->entries[i].flags,
               sizeof(uint64_t));
      }
    }
  }

  // { u64                   abi; # enum perf_sample_regs_abi
  //   u64                   regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
  if (sample_fields & PERF_SAMPLE_REGS_USER) {
    LOG(ERROR) << "PERF_SAMPLE_REGS_USER is not yet supported.";
    return (array - initial_array_ptr) * sizeof(uint64_t);
  }

  // { u64                   size;
  //   char                  data[size];
  //   u64                   dyn_size; } && PERF_SAMPLE_STACK_USER
  if (sample_fields & PERF_SAMPLE_STACK_USER) {
    LOG(ERROR) << "PERF_SAMPLE_STACK_USER is not yet supported.";
    return (array - initial_array_ptr) * sizeof(uint64_t);
  }

  // { u64                   weight;   } && PERF_SAMPLE_WEIGHT
  if (sample_fields & PERF_SAMPLE_WEIGHT) {
    *array++ = sample.weight;
  }

  // { u64                   data_src; } && PERF_SAMPLE_DATA_SRC
  if (sample_fields & PERF_SAMPLE_DATA_SRC) {
    *array++ = sample.data_src;
  }

  // { u64                   transaction; } && PERF_SAMPLE_TRANSACTION
  if (sample_fields & PERF_SAMPLE_TRANSACTION) {
    *array++ = sample.transaction;
  }

  return (array - initial_array_ptr) * sizeof(uint64_t);
}

}  // namespace

bool SampleInfoReader::ReadPerfSampleInfo(const event_t& event,
                                          struct perf_sample* sample) const {
  CHECK(sample);

  if (!IsSupportedEventType(event.header.type)) {
    LOG(ERROR) << "Unsupported event type " << event.header.type;
    return false;
  }

  size_t size_read_or_skipped = ReadPerfSampleFromData(
      event, event_attr_, read_cross_endian_, sample);

  if (size_read_or_skipped != event.header.size) {
    LOG(ERROR) << "Read/skipped " << size_read_or_skipped << " bytes, expected "
               << event.header.size << " bytes.";
  }

  return (size_read_or_skipped == event.header.size);
}

bool SampleInfoReader::WritePerfSampleInfo(const perf_sample& sample,
                                           event_t* event) const {
  CHECK(event);

  if (!IsSupportedEventType(event->header.type)) {
    LOG(ERROR) << "Unsupported event type " << event->header.type;
    return false;
  }

  size_t size_written_or_skipped =
      WritePerfSampleToData(sample, event_attr_, event);
  if (size_written_or_skipped != event->header.size) {
    LOG(ERROR) << "Wrote/skipped " << size_written_or_skipped
               << " bytes, expected " << event->header.size << " bytes.";
  }

  return (size_written_or_skipped == event->header.size);
}

// static
uint64_t SampleInfoReader::GetSampleFieldsForEventType(uint32_t event_type,
                                                       uint64_t sample_type) {
  uint64_t mask = UINT64_MAX;
  switch (event_type) {
  case PERF_RECORD_MMAP:
  case PERF_RECORD_LOST:
  case PERF_RECORD_COMM:
  case PERF_RECORD_EXIT:
  case PERF_RECORD_THROTTLE:
  case PERF_RECORD_UNTHROTTLE:
  case PERF_RECORD_FORK:
  case PERF_RECORD_READ:
  case PERF_RECORD_MMAP2:
    // See perf_event.h "struct" sample_id and sample_id_all.
    mask = PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ID |
           PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER;
    break;
  case PERF_RECORD_SAMPLE:
    break;
  default:
    LOG(FATAL) << "Unknown event type " << event_type;
  }
  return sample_type & mask;
}

// static
uint64_t SampleInfoReader::GetPerfSampleDataOffset(const event_t& event) {
  uint64_t offset = UINT64_MAX;
  switch (event.header.type) {
  case PERF_RECORD_SAMPLE:
    offset = offsetof(event_t, sample.array);
    break;
  case PERF_RECORD_MMAP:
    offset = sizeof(event.mmap) - sizeof(event.mmap.filename) +
             GetUint64AlignedStringLength(event.mmap.filename);
    break;
  case PERF_RECORD_FORK:
  case PERF_RECORD_EXIT:
    offset = sizeof(event.fork);
    break;
  case PERF_RECORD_COMM:
    offset = sizeof(event.comm) - sizeof(event.comm.comm) +
             GetUint64AlignedStringLength(event.comm.comm);
    break;
  case PERF_RECORD_LOST:
    offset = sizeof(event.lost);
    break;
  case PERF_RECORD_THROTTLE:
  case PERF_RECORD_UNTHROTTLE:
    offset = sizeof(event.throttle);
    break;
  case PERF_RECORD_READ:
    offset = sizeof(event.read);
    break;
  case PERF_RECORD_MMAP2:
    offset = sizeof(event.mmap2) - sizeof(event.mmap2.filename) +
             GetUint64AlignedStringLength(event.mmap2.filename);
    break;
  default:
    LOG(FATAL) << "Unknown event type " << event.header.type;
    break;
  }
  // Make sure the offset was valid
  CHECK_NE(offset, UINT64_MAX);
  CHECK_EQ(offset % sizeof(uint64_t), 0U);
  return offset;
}

}  // namespace quipper
