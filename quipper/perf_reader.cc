// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <algorithm>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"

#include "binary_data_utils.h"
#include "buffer_reader.h"
#include "buffer_writer.h"
#include "compat/string.h"
#include "file_reader.h"
#include "file_utils.h"
#include "perf_data_structures.h"
#include "perf_data_utils.h"
#include "sample_info_reader.h"

namespace quipper {

using PerfEvent = PerfDataProto_PerfEvent;
using SampleInfo = PerfDataProto_SampleInfo;
using StringAndMd5sumPrefix =
    PerfDataProto_StringMetadata_StringAndMd5sumPrefix;

namespace {

// The type of the storage size of a string, prefixed before each string field
// in raw data.
typedef u32 string_size_type;

// The type of the number of string data, found in the command line metadata in
// the perf data file.
typedef u32 num_string_data_type;

// Types of the event desc fields that are not found in other structs.
typedef u32 event_desc_num_events;
typedef u32 event_desc_attr_size;
typedef u32 event_desc_num_unique_ids;

// The type of the number of nodes field in NUMA topology.
typedef u32 numa_topology_num_nodes_type;

// The type of the number of mappings in pmu mappings.
typedef u32 pmu_mappings_num_mappings_type;

// The type of the number of groups in group desc.
typedef u32 group_desc_num_groups_type;

// A mask that is applied to |metadata_mask()| in order to get a mask for
// only the metadata supported by quipper.
const uint32_t kSupportedMetadataMask =
    1 << HEADER_TRACING_DATA | 1 << HEADER_BUILD_ID | 1 << HEADER_HOSTNAME |
    1 << HEADER_OSRELEASE | 1 << HEADER_VERSION | 1 << HEADER_ARCH |
    1 << HEADER_NRCPUS | 1 << HEADER_CPUDESC | 1 << HEADER_CPUID |
    1 << HEADER_TOTAL_MEM | 1 << HEADER_CMDLINE | 1 << HEADER_EVENT_DESC |
    1 << HEADER_CPU_TOPOLOGY | 1 << HEADER_NUMA_TOPOLOGY |
    1 << HEADER_BRANCH_STACK | 1 << HEADER_PMU_MAPPINGS |
    1 << HEADER_GROUP_DESC;

// By default, the build ID event has PID = -1.
const uint32_t kDefaultBuildIDEventPid = static_cast<uint32_t>(-1);

// Eight bits in a byte.
size_t BytesToBits(size_t num_bytes) { return num_bytes * 8; }

u8 ReverseByte(u8 x) {
  x = (x & 0xf0) >> 4 | (x & 0x0f) << 4;  // exchange nibbles
  x = (x & 0xcc) >> 2 | (x & 0x33) << 2;  // exchange pairs
  x = (x & 0xaa) >> 1 | (x & 0x55) << 1;  // exchange neighbors
  return x;
}

// If field points to the start of a bitfield padded to len bytes, this
// performs an endian swap of the bitfield, assuming the compiler that produced
// it conforms to the same ABI (bitfield layout is not completely specified by
// the language).
void SwapBitfieldOfBits(u8* field, size_t len) {
  for (size_t i = 0; i < len; i++) {
    field[i] = ReverseByte(field[i]);
  }
}

// The code currently assumes that the compiler will not add any padding to the
// various structs.  These CHECKs make sure that this is true.
void CheckNoEventHeaderPadding() {
  perf_event_header header;
  CHECK_EQ(sizeof(header),
           sizeof(header.type) + sizeof(header.misc) + sizeof(header.size));
}

void CheckNoPerfEventAttrPadding() {
  perf_event_attr attr;
  CHECK_EQ(sizeof(attr), (reinterpret_cast<u64>(&attr.__reserved_2) -
                          reinterpret_cast<u64>(&attr)) +
                             sizeof(attr.__reserved_2));
}

void CheckNoEventTypePadding() {
  perf_trace_event_type event_type;
  CHECK_EQ(sizeof(event_type),
           sizeof(event_type.event_id) + sizeof(event_type.name));
}

void CheckNoBuildIDEventPadding() {
  build_id_event event;
  CHECK_EQ(sizeof(event), sizeof(event.header.type) +
                              sizeof(event.header.misc) +
                              sizeof(event.header.size) + sizeof(event.pid) +
                              sizeof(event.build_id));
}

// Creates a new build ID event with the given build ID string, filename, and
// misc value.
malloced_unique_ptr<build_id_event> CreateBuildIDEvent(const string& build_id,
                                                       const string& filename,
                                                       uint16_t misc) {
  size_t filename_len = GetUint64AlignedStringLength(filename);
  size_t size = sizeof(struct build_id_event) + filename_len;
  malloced_unique_ptr<build_id_event> event(CallocMemoryForBuildID(size));
  event->header.type = HEADER_BUILD_ID;
  event->header.misc = misc;
  event->header.size = size;

  event->pid = kDefaultBuildIDEventPid;
  snprintf(event->filename, filename_len, "%s", filename.c_str());
  memset(event->filename + filename.size(), 0, filename_len - filename.size());

  HexStringToRawData(build_id, event->build_id, sizeof(event->build_id));
  return event;
}

// Given a string, returns the total size required to store the string in perf
// data, including a preceding length field and extra padding to align the
// string + null terminator to a multiple of uint64s.
size_t ExpectedStorageSizeOf(const string& str) {
  return sizeof(string_size_type) + GetUint64AlignedStringLength(str);
}

// Reads a perf_event_header from |data| and performs byte swapping if
// necessary. Returns true on success, or false if there was an error.
bool ReadPerfEventHeader(DataReader* data, struct perf_event_header* header) {
  if (!data->ReadData(sizeof(struct perf_event_header), header)) {
    LOG(ERROR) << "Error reading perf event header.";
    return false;
  }
  if (data->is_cross_endian()) {
    ByteSwap(&header->type);
    ByteSwap(&header->size);
    ByteSwap(&header->misc);
  }
  return true;
}

// Reads a perf_file_section from |data| and performs byte swapping if
// necessary. Returns true on success, or false if there was an error.
bool ReadPerfFileSection(DataReader* data, struct perf_file_section* section) {
  if (!data->ReadData(sizeof(*section), section)) {
    LOG(ERROR) << "Error reading perf file section info.";
    return false;
  }
  if (data->is_cross_endian()) {
    ByteSwap(&section->offset);
    ByteSwap(&section->size);
  }
  return true;
}

// Returns true if |e1| has an earlier timestamp than |e2|. Used to sort an
// array of events.
static bool CompareEventTimes(const PerfEvent* e1, const PerfEvent* e2) {
  return e1->timestamp() < e2->timestamp();
}

}  // namespace

PerfReader::PerfReader()
    : proto_(Arena::CreateMessage<PerfDataProto>(&arena_)),
      is_cross_endian_(false) {
  // The metadata mask is stored in |proto_|. It should be initialized to 0
  // since it is used heavily.
  proto_->add_metadata_mask(0);
}

PerfReader::~PerfReader() {}

bool PerfReader::Serialize(PerfDataProto* perf_data_proto) const {
  perf_data_proto->CopyFrom(*proto_);

  // Add a timestamp_sec to the protobuf.
  struct timeval timestamp_sec;
  if (!gettimeofday(&timestamp_sec, NULL))
    perf_data_proto->set_timestamp_sec(timestamp_sec.tv_sec);
  return true;
}

bool PerfReader::Deserialize(const PerfDataProto& perf_data_proto) {
  proto_->CopyFrom(perf_data_proto);

  // Iterate through all attrs and create a SampleInfoReader for each of them.
  // This is necessary for writing the proto representation of perf data to raw
  // data.
  for (const auto& stored_attr : proto_->file_attrs()) {
    PerfFileAttr attr;
    serializer_.DeserializePerfFileAttr(stored_attr, &attr);
    serializer_.CreateSampleInfoReader(attr, false /* read_cross_endian */);
  }
  return true;
}

bool PerfReader::ReadFile(const string& filename) {
  FileReader reader(filename);
  if (!reader.IsOpen()) {
    LOG(ERROR) << "Unable to open file " << filename;
    return false;
  }
  return ReadFromData(&reader);
}

bool PerfReader::ReadFromVector(const std::vector<char>& data) {
  return ReadFromPointer(data.data(), data.size());
}

bool PerfReader::ReadFromString(const string& str) {
  return ReadFromPointer(str.data(), str.size());
}

bool PerfReader::ReadFromPointer(const char* data, size_t size) {
  BufferReader buffer(data, size);
  return ReadFromData(&buffer);
}

bool PerfReader::ReadFromData(DataReader* data) {
  if (data->size() == 0) {
    LOG(ERROR) << "Input data is empty";
    return false;
  }
  if (!ReadHeader(data)) return false;

  // Check if it is normal perf data.
  if (header_.size == sizeof(header_)) {
    DVLOG(1) << "Perf data is in normal format.";
    return ReadFileData(data);
  }

  // Otherwise it is piped data.
  if (piped_header_.size != sizeof(piped_header_)) {
    LOG(ERROR) << "Expecting piped data format, but header size "
               << piped_header_.size << " does not match expected size "
               << sizeof(piped_header_);
    return false;
  }

  return ReadPipedData(data);
}

bool PerfReader::WriteFile(const string& filename) {
  std::vector<char> data;
  return WriteToVector(&data) && BufferToFile(filename, data);
}

bool PerfReader::WriteToVector(std::vector<char>* data) {
  data->resize(GetSize());
  return WriteToPointerWithoutCheckingSize(&data->at(0), data->size());
}

bool PerfReader::WriteToString(string* str) {
  str->resize(GetSize());
  return WriteToPointerWithoutCheckingSize(&str->at(0), str->size());
}

bool PerfReader::WriteToPointer(char* buffer, size_t size) {
  size_t required_size = GetSize();
  if (size < required_size) {
    LOG(ERROR) << "Buffer is too small - buffer size is " << size
               << " and required size is " << required_size;
    return false;
  }
  return WriteToPointerWithoutCheckingSize(buffer, size);
}

bool PerfReader::WriteToPointerWithoutCheckingSize(char* buffer, size_t size) {
  BufferWriter data(buffer, size);
  struct perf_file_header header;
  GenerateHeader(&header);

  return WriteHeader(header, &data) && WriteAttrs(header, &data) &&
         WriteData(header, &data) && WriteMetadata(header, &data);
}

size_t PerfReader::GetSize() const {
  struct perf_file_header header;
  GenerateHeader(&header);

  size_t total_size = 0;
  total_size += header.size;
  total_size += header.attrs.size;
  total_size += header.event_types.size;
  total_size += header.data.size;
  // Add the ID info, whose size is not explicitly included in the header.
  for (const auto& attr : proto_->file_attrs()) {
    total_size +=
        attr.ids_size() * sizeof(decltype(PerfFileAttr::ids)::value_type);
  }

  // Additional info about metadata.  See WriteMetadata for explanation.
  total_size += GetNumSupportedMetadata() * sizeof(struct perf_file_section);

  // Add the sizes of the various metadata.
  total_size += tracing_data().size();
  total_size += GetBuildIDMetadataSize();
  total_size += GetStringMetadataSize();
  total_size += GetUint32MetadataSize();
  total_size += GetUint64MetadataSize();
  total_size += GetEventDescMetadataSize();
  total_size += GetCPUTopologyMetadataSize();
  total_size += GetNUMATopologyMetadataSize();
  total_size += GetPMUMappingsMetadataSize();
  total_size += GetGroupDescMetadataSize();
  return total_size;
}

void PerfReader::GenerateHeader(struct perf_file_header* header) const {
  // This is the order of the input perf file contents in normal mode:
  // 1. Header
  // 2. Attribute IDs (pointed to by attr.ids.offset)
  // 3. Attributes
  // 4. Event types
  // 5. Data
  // 6. Metadata

  // Compute offsets in the above order.
  CheckNoEventHeaderPadding();
  memset(header, 0, sizeof(*header));
  header->magic = kPerfMagic;
  header->size = sizeof(*header);
  header->attr_size = sizeof(perf_file_attr);
  header->attrs.size = header->attr_size * attrs().size();
  for (const PerfEvent& event : proto_->events()) {
    header->data.size += event.header().size();
    // Auxtrace event contain trace data at the end of the event. Add the size
    // of this trace data to the header.data size.
    if (event.header().type() == PERF_RECORD_AUXTRACE) {
      header->data.size += event.auxtrace_event().size();
    }
  }
  // Do not use the event_types section. Use EVENT_DESC metadata instead.
  header->event_types.size = 0;

  u64 current_offset = 0;
  current_offset += header->size;
  for (const auto& attr : proto_->file_attrs()) {
    current_offset +=
        sizeof(decltype(PerfFileAttr::ids)::value_type) * attr.ids_size();
  }
  header->attrs.offset = current_offset;
  current_offset += header->attrs.size;
  header->event_types.offset = current_offset;
  current_offset += header->event_types.size;

  header->data.offset = current_offset;

  // Construct the header feature bits.
  memset(&header->adds_features, 0, sizeof(header->adds_features));
  // The following code makes the assumption that all feature bits are in the
  // first word of |adds_features|.  If the perf data format changes and the
  // assumption is no longer valid, this CHECK will fail, at which point the
  // below code needs to be updated.  For now, sticking to that assumption keeps
  // the code simple.
  // This assumption is also used when reading metadata, so that code
  // will also have to be updated if this CHECK starts to fail.
  CHECK_LE(static_cast<size_t>(HEADER_LAST_FEATURE),
           BytesToBits(sizeof(header->adds_features[0])));
  header->adds_features[0] |= metadata_mask() & kSupportedMetadataMask;
}

bool PerfReader::InjectBuildIDs(
    const std::map<string, string>& filenames_to_build_ids) {
  set_metadata_mask_bit(HEADER_BUILD_ID);
  std::set<string> updated_filenames;
  // Inject new build ID's for existing build ID events.
  for (auto& build_id : *proto_->mutable_build_ids()) {
    auto find_result = filenames_to_build_ids.find(build_id.filename());
    if (find_result == filenames_to_build_ids.end()) continue;
    const string& build_id_string = find_result->second;
    const int kHexCharsPerByte = 2;
    std::vector<uint8_t> build_id_data(build_id_string.size() /
                                       kHexCharsPerByte);
    if (!HexStringToRawData(build_id_string, build_id_data.data(),
                            build_id_data.size())) {
      LOG(ERROR) << "Could not convert hex string to raw data: "
                 << build_id_string;
      return false;
    }
    build_id.set_build_id_hash(build_id_data.data(), build_id_data.size());

    updated_filenames.insert(build_id.filename());
  }

  // For files with no existing build ID events, create new build ID events.
  // This requires a lookup of all MMAP's to determine the |misc| field of each
  // build ID event.
  std::map<string, uint16_t> filename_to_misc;
  for (const PerfEvent& event : proto_->events()) {
    if (event.header().type() == PERF_RECORD_MMAP ||
        event.header().type() == PERF_RECORD_MMAP2) {
      filename_to_misc[event.mmap_event().filename()] = event.header().misc();
    }
  }

  std::map<string, string>::const_iterator it;
  for (it = filenames_to_build_ids.begin(); it != filenames_to_build_ids.end();
       ++it) {
    const string& filename = it->first;
    if (updated_filenames.find(filename) != updated_filenames.end()) continue;

    // Determine the misc field.
    uint16_t new_misc = PERF_RECORD_MISC_KERNEL;
    std::map<string, uint16_t>::const_iterator misc_iter =
        filename_to_misc.find(filename);
    if (misc_iter != filename_to_misc.end()) new_misc = misc_iter->second;

    string build_id = it->second;
    malloced_unique_ptr<build_id_event> event =
        CreateBuildIDEvent(build_id, filename, new_misc);
    if (!serializer_.SerializeBuildIDEvent(event, proto_->add_build_ids())) {
      LOG(ERROR) << "Could not serialize build ID event with ID " << build_id;
      return false;
    }
  }
  return true;
}

bool PerfReader::Localize(
    const std::map<string, string>& build_ids_to_filenames) {
  std::map<string, string> filename_map;
  for (auto& build_id : *proto_->mutable_build_ids()) {
    string build_id_string = RawDataToHexString(build_id.build_id_hash());
    auto find_result = build_ids_to_filenames.find(build_id_string);
    if (find_result == build_ids_to_filenames.end()) continue;
    const string& new_filename = find_result->second;
    filename_map[build_id.filename()] = new_filename;
  }

  return LocalizeUsingFilenames(filename_map);
}

bool PerfReader::LocalizeUsingFilenames(
    const std::map<string, string>& filename_map) {
  LocalizeMMapFilenames(filename_map);
  for (auto& build_id : *proto_->mutable_build_ids()) {
    auto find_result = filename_map.find(build_id.filename());
    if (find_result != filename_map.end())
      build_id.set_filename(find_result->second);
  }
  return true;
}

void PerfReader::GetFilenames(std::vector<string>* filenames) const {
  std::set<string> filename_set;
  GetFilenamesAsSet(&filename_set);
  filenames->clear();
  filenames->insert(filenames->begin(), filename_set.begin(),
                    filename_set.end());
}

void PerfReader::GetFilenamesAsSet(std::set<string>* filenames) const {
  filenames->clear();
  for (const PerfEvent& event : proto_->events()) {
    if (event.header().type() == PERF_RECORD_MMAP ||
        event.header().type() == PERF_RECORD_MMAP2) {
      filenames->insert(event.mmap_event().filename());
    }
  }
}

void PerfReader::GetFilenamesToBuildIDs(
    std::map<string, string>* filenames_to_build_ids) const {
  filenames_to_build_ids->clear();
  for (const auto& build_id : proto_->build_ids()) {
    string build_id_string = RawDataToHexString(build_id.build_id_hash());
    PerfizeBuildIDString(&build_id_string);
    (*filenames_to_build_ids)[build_id.filename()] = build_id_string;
  }
}

void PerfReader::MaybeSortEventsByTime() {
  // Events can not be sorted by time if PERF_SAMPLE_TIME is not set in
  // attr.sample_type for all attrs.
  for (const auto& attr : attrs()) {
    if (!(attr.attr().sample_type() & PERF_SAMPLE_TIME)) {
      return;
    }
  }

  // Sort the events based on timestamp.

  // This sorts the pointers in the proto-internal vector, which
  // requires no copying and less external space.
  std::stable_sort(proto_->mutable_events()->pointer_begin(),
                   proto_->mutable_events()->pointer_end(), CompareEventTimes);
}

bool PerfReader::ReadHeader(DataReader* data) {
  CheckNoEventHeaderPadding();
  // The header is the first thing to be read. Don't use SeekSet(0) because it
  // doesn't make sense for piped files. Instead, verify that the reader points
  // to the start of the data.
  CHECK_EQ(0U, data->Tell());
  if (!data->ReadUint64(&piped_header_.magic)) {
    LOG(ERROR) << "Error reading header magic number.";
    return false;
  }

  if (piped_header_.magic != kPerfMagic &&
      piped_header_.magic != bswap_64(kPerfMagic)) {
    // clang-format off
    LOG(ERROR) << "Read wrong magic. Expected: 0x" << std::hex << kPerfMagic
               << " or 0x" << std::hex << bswap_64(kPerfMagic)
               << " Got: 0x" << std::hex << piped_header_.magic;
    // clang-format on
    return false;
  }
  is_cross_endian_ = (piped_header_.magic != kPerfMagic);
  data->set_is_cross_endian(is_cross_endian_);

  if (!data->ReadUint64(&piped_header_.size)) {
    LOG(ERROR) << "Error reading header size.";
    return false;
  }

  CHECK_EQ(data->Tell(), sizeof(piped_header_));

  // Header can be a piped header.
  if (piped_header_.size == sizeof(piped_header_)) return true;

  // Read as a non-piped header.
  if (!data->ReadUint64(&header_.attr_size)) {
    LOG(ERROR) << "Error reading header::attr_size.";
    return false;
  }
  if (!ReadPerfFileSection(data, &header_.attrs) ||
      !ReadPerfFileSection(data, &header_.data) ||
      !ReadPerfFileSection(data, &header_.event_types)) {
    LOG(ERROR) << "Error reading header file section info.";
    return false;
  }

  const size_t features_size = sizeof(header_.adds_features);
  CHECK_EQ(data->Tell(), sizeof(header_) - features_size);

  if (!data->ReadData(features_size, header_.adds_features)) {
    LOG(ERROR) << "Error reading header::adds_features.";
    return false;
  }
  proto_->set_metadata_mask(0, header_.adds_features[0]);

  // Byte-swapping |adds_features| is tricky. It is defined as an array of
  // unsigned longs, which can vary between architectures. However, the overall
  // size of the array in bytes is fixed.
  //
  // According to perf's perf_file_header__read() function, the hostname feature
  // should always be set. Try byte-swapping as uint64s first and check the
  // hostname bit. If it's not set, then try swapping as uint32s. This is
  // similar to the algorithm used in perf.
  if (data->is_cross_endian()) {
    static_assert(sizeof(header_.adds_features[0]) == sizeof(uint32_t) ||
                      sizeof(header_.adds_features[0]) == sizeof(uint64_t),
                  "|header_.adds_features| must be defined as an array of "
                  "either 32-bit or 64-bit words.");

    uint64_t features64 = 0;
    // Some compilers will complain if we directly cast |header_.adds_features|
    // to a uint64_t*. Instead, determine the first uint64_t without using
    // pointer aliasing.
    if (sizeof(header_.adds_features[0]) == sizeof(uint64_t)) {
      features64 = bswap_64(header_.adds_features[0]);
    } else {
      // If the native |adds_features| is composed of 32-bit words, swap the
      // byte order of each word and then swap their positions to create a
      // 64-bit word.
      features64 = static_cast<uint64_t>(bswap_32(header_.adds_features[0]))
                   << 32;
      features64 |= bswap_32(header_.adds_features[1]);
    }
    if (features64 & (1 << HEADER_HOSTNAME)) {
      for (size_t i = 0; i < features_size / sizeof(uint64_t); ++i)
        ByteSwap(reinterpret_cast<uint64_t*>(header_.adds_features) + i);
    } else {
      for (size_t i = 0; i < features_size / sizeof(uint32_t); ++i)
        ByteSwap(reinterpret_cast<uint32_t*>(header_.adds_features) + i);
    }
  }

  return true;
}

bool PerfReader::ReadAttrsSection(DataReader* data) {
  size_t num_attrs = header_.attrs.size / header_.attr_size;
  if (header_.attrs.size % header_.attr_size != 0) {
    LOG(ERROR) << "Total size of attrs " << header_.attrs.size
               << " is not a multiple of attr size " << header_.attr_size;
  }
  data->SeekSet(header_.attrs.offset);
  for (size_t i = 0; i < num_attrs; i++) {
    if (!ReadAttr(data)) return false;
  }
  return true;
}

bool PerfReader::ReadAttr(DataReader* data) {
  PerfFileAttr attr;
  if (!ReadEventAttr(data, &attr.attr)) return false;

  perf_file_section ids;
  if (!ReadPerfFileSection(data, &ids)) return false;

  // The ID may be stored at a different location in the file than where we're
  // currently reading.
  size_t saved_offset = data->Tell();
  data->SeekSet(ids.offset);

  size_t num_ids = ids.size / sizeof(decltype(attr.ids)::value_type);
  if (!ReadUniqueIDs(data, num_ids, &attr.ids)) return false;
  data->SeekSet(saved_offset);
  AddPerfFileAttr(attr);

  return true;
}

bool PerfReader::ReadEventAttr(DataReader* data, perf_event_attr* attr) {
  CheckNoPerfEventAttrPadding();
  *attr = {0};

  static_assert(
      offsetof(struct perf_event_attr, size) == sizeof(perf_event_attr::type),
      "type and size should be the first to fields of perf_event_attr");

  if (!data->ReadUint32(&attr->type) || !data->ReadUint32(&attr->size)) {
    LOG(ERROR) << "Error reading event attr type and size.";
    return false;
  }

  // Now read the rest of the attr struct.
  const size_t attr_offset = sizeof(attr->type) + sizeof(attr->size);
  const size_t attr_readable_size =
      std::min(static_cast<size_t>(attr->size), sizeof(*attr));
  if (!data->ReadDataValue(attr_readable_size - attr_offset, "attribute",
                           reinterpret_cast<char*>(attr) + attr_offset)) {
    return false;
  }
  data->SeekSet(data->Tell() + attr->size - attr_readable_size);

  if (data->is_cross_endian()) {
    // Depending on attr->size, some of these might not have actually been
    // read. This is okay: they are zero.
    ByteSwap(&attr->type);
    ByteSwap(&attr->size);
    ByteSwap(&attr->config);
    ByteSwap(&attr->sample_period);
    ByteSwap(&attr->sample_type);
    ByteSwap(&attr->read_format);

    // NB: This will also reverse precise_ip : 2 as if it was two fields:
    auto* const bitfield_start = &attr->read_format + 1;
    SwapBitfieldOfBits(reinterpret_cast<u8*>(bitfield_start), sizeof(u64));
    // ... So swap it back:
    const auto tmp = attr->precise_ip;
    attr->precise_ip = (tmp & 0x2) >> 1 | (tmp & 0x1) << 1;

    ByteSwap(&attr->wakeup_events);  // union with wakeup_watermark
    ByteSwap(&attr->bp_type);
    ByteSwap(&attr->bp_addr);  // union with config1
    ByteSwap(&attr->bp_len);   // union with config2
    ByteSwap(&attr->branch_sample_type);
    ByteSwap(&attr->sample_regs_user);
    ByteSwap(&attr->sample_stack_user);
  }

  // The actual perf_event_attr data size might be different from the size of
  // the struct definition.  Check against perf_event_attr's |size| field.
  attr->size = sizeof(*attr);

  return true;
}

bool PerfReader::ReadUniqueIDs(DataReader* data, size_t num_ids,
                               std::vector<u64>* ids) {
  ids->resize(num_ids);
  for (u64& id : *ids) {
    if (!data->ReadUint64(&id)) {
      LOG(ERROR) << "Error reading unique ID.";
      return false;
    }
  }
  return true;
}

bool PerfReader::ReadEventTypesSection(DataReader* data) {
  int num_event_types =
      header_.event_types.size / sizeof(struct perf_trace_event_type);
  if (num_event_types == 0) {
    // Not available.
    return true;
  }
  CHECK_EQ(proto_->file_attrs().size(), num_event_types);
  CHECK_EQ(sizeof(perf_trace_event_type) * num_event_types,
           header_.event_types.size);
  data->SeekSet(header_.event_types.offset);
  for (int i = 0; i < num_event_types; ++i) {
    if (!ReadEventType(data, i, 0)) return false;
  }
  return true;
}

bool PerfReader::ReadEventType(DataReader* data, int attr_idx,
                               size_t event_size) {
  CheckNoEventTypePadding();
  decltype(perf_trace_event_type::event_id) event_id;

  if (!data->ReadUint64(&event_id)) {
    LOG(ERROR) << "Error reading event ID.";
    return false;
  }

  size_t event_name_len;
  if (event_size == 0) {  // Not in an event.
    event_name_len = sizeof(perf_trace_event_type::name);
  } else {
    event_name_len = event_size - sizeof(perf_event_header) - sizeof(event_id);
  }

  PerfFileAttr attr;
  if (!data->ReadString(event_name_len, &attr.name)) {
    LOG(ERROR) << "Not enough data left in data to read event name.";
    return false;
  }

  if (attr_idx >= proto_->file_attrs().size()) {
    LOG(ERROR) << "Too many event types, or attrs not read yet!";
    return false;
  }
  if (event_id != proto_->file_attrs(attr_idx).attr().config()) {
    LOG(ERROR) << "event_id for perf_trace_event_type (" << event_id << ") "
               << "does not match attr.config ("
               << proto_->file_attrs(attr_idx).attr().config() << ")";
    return false;
  }
  attr.attr.config = proto_->file_attrs(attr_idx).attr().config();

  serializer_.SerializePerfEventType(attr, proto_->add_event_types());
  return true;
}

bool PerfReader::ReadDataSection(DataReader* data) {
  u64 data_remaining_bytes = header_.data.size;
  data->SeekSet(header_.data.offset);
  while (data_remaining_bytes != 0) {
    // Read the header to determine the size of the event.
    perf_event_header header;
    if (!ReadPerfEventHeader(data, &header)) {
      LOG(ERROR) << "Error reading event header from data section.";
      return false;
    }

    // Read the rest of the event data.
    malloced_unique_ptr<event_t> event(CallocMemoryForEvent(header.size));
    event->header = header;
    if (!data->ReadDataValue(event->header.size - sizeof(event->header),
                             "rest of event", &event->header + 1)) {
      return false;
    }
    MaybeSwapEventFields(event.get(), data->is_cross_endian());

    // We must have a valid way to read sample info before reading perf events.
    CHECK(serializer_.SampleInfoReaderAvailable());

    // Serialize the event to protobuf form.
    PerfEvent* proto_event = proto_->add_events();
    if (!serializer_.SerializeEvent(event, proto_event)) return false;

    if (proto_event->header().type() == PERF_RECORD_AUXTRACE) {
      if (!ReadAuxtraceTraceData(data, proto_event)) return false;
      data_remaining_bytes -= proto_event->auxtrace_event().size();
    }
    data_remaining_bytes -= event->header.size;
  }

  DLOG(INFO) << "Number of events stored: " << proto_->events_size();
  return true;
}

bool PerfReader::ReadMetadata(DataReader* data) {
  // Metadata comes after the event data.
  data->SeekSet(header_.data.offset + header_.data.size);

  // Read the (offset, size) pairs of all the metadata elements. Note that this
  // takes into account all present metadata types, not just the ones included
  // in |kSupportedMetadataMask|. If a metadata type is not supported, it is
  // skipped over.
  std::vector<struct perf_file_section> sections(GetNumBits(metadata_mask()));
  for (struct perf_file_section& section : sections) {
    if (!ReadPerfFileSection(data, &section)) {
      LOG(ERROR) << "Error reading metadata entry info.";
      return false;
    }
  }

  auto section_iter = sections.begin();
  for (u32 type = HEADER_FIRST_FEATURE; type != HEADER_LAST_FEATURE; ++type) {
    if (!get_metadata_mask_bit(type)) continue;
    data->SeekSet(section_iter->offset);
    u64 size = section_iter->size;

    switch (type) {
      case HEADER_TRACING_DATA:
        if (!ReadTracingMetadata(data, size)) return false;
        break;
      case HEADER_BUILD_ID:
        if (!ReadBuildIDMetadata(data, size)) return false;
        break;
      case HEADER_HOSTNAME:
        if (!ReadSingleStringMetadata(
                data, size,
                proto_->mutable_string_metadata()->mutable_hostname())) {
          return false;
        }
        break;
      case HEADER_OSRELEASE:
        if (!ReadSingleStringMetadata(
                data, size,
                proto_->mutable_string_metadata()->mutable_kernel_version())) {
          return false;
        }
        break;
      case HEADER_VERSION:
        if (!ReadSingleStringMetadata(
                data, size,
                proto_->mutable_string_metadata()->mutable_perf_version())) {
          return false;
        }
        break;
      case HEADER_ARCH:
        if (!ReadSingleStringMetadata(
                data, size,
                proto_->mutable_string_metadata()->mutable_architecture())) {
          return false;
        }
        break;
      case HEADER_CPUDESC:
        if (!ReadSingleStringMetadata(
                data, size,
                proto_->mutable_string_metadata()->mutable_cpu_description())) {
          return false;
        }
        break;
      case HEADER_CPUID:
        if (!ReadSingleStringMetadata(
                data, size,
                proto_->mutable_string_metadata()->mutable_cpu_id())) {
          return false;
        }
        break;
      case HEADER_CMDLINE: {
        auto* string_metadata = proto_->mutable_string_metadata();
        if (!ReadRepeatedStringMetadata(
                data, size, string_metadata->mutable_perf_command_line_token(),
                string_metadata->mutable_perf_command_line_whole())) {
          return false;
        }
        break;
      }
      case HEADER_NRCPUS:
        if (!ReadUint32Metadata(data, type, size)) return false;
        break;
      case HEADER_TOTAL_MEM:
        if (!ReadUint64Metadata(data, type, size)) return false;
        break;
      case HEADER_EVENT_DESC:
        if (!ReadEventDescMetadata(data)) return false;
        break;
      case HEADER_CPU_TOPOLOGY:
        if (!ReadCPUTopologyMetadata(data)) return false;
        break;
      case HEADER_NUMA_TOPOLOGY:
        if (!ReadNUMATopologyMetadata(data)) return false;
        break;
      case HEADER_BRANCH_STACK:
        break;
      case HEADER_PMU_MAPPINGS:
        if (!ReadPMUMappingsMetadata(data, size)) return false;
        break;
      case HEADER_GROUP_DESC:
        if (!ReadGroupDescMetadata(data)) return false;
        break;
      default:
        LOG(INFO) << "Unsupported metadata type, skipping: " << type;
        break;
    }
    ++section_iter;
  }

  return true;
}

bool PerfReader::ReadBuildIDMetadata(DataReader* data, size_t size) {
  CheckNoBuildIDEventPadding();
  while (size > 0) {
    // Make sure there is enough data for everything but the filename.
    perf_event_header build_id_header;
    if (!ReadPerfEventHeader(data, &build_id_header)) {
      LOG(ERROR) << "Error reading build ID header.";
      return false;
    }

    if (!ReadBuildIDMetadataWithoutHeader(data, build_id_header)) return false;
    size -= build_id_header.size;
  }

  return true;
}

bool PerfReader::ReadBuildIDMetadataWithoutHeader(
    DataReader* data, const perf_event_header& header) {
  // Allocate memory for the event.
  malloced_unique_ptr<build_id_event> event(
      CallocMemoryForBuildID(header.size));
  event->header = header;

  // Make sure there is enough data for the rest of the event.
  if (!data->ReadDataValue(header.size - sizeof(header),
                           "rest of build ID event", &event->header + 1)) {
    LOG(ERROR) << "Not enough bytes to read build id event";
    return false;
  }
  if (data->is_cross_endian()) ByteSwap(&event->pid);

  // Perf tends to use more space than necessary, so fix the size.
  event->header.size =
      sizeof(*event) + GetUint64AlignedStringLength(event->filename);

  if (!serializer_.SerializeBuildIDEvent(event, proto_->add_build_ids())) {
    LOG(ERROR) << "Could not serialize build ID event with ID "
               << RawDataToHexString(event->build_id, sizeof(event->build_id));
    return false;
  }
  return true;
}

bool PerfReader::ReadSingleStringMetadata(DataReader* data,
                                          size_t max_readable_size,
                                          StringAndMd5sumPrefix* dest) const {
  // If a string metadata field is present but empty, it can have a size of 0,
  // in which case there is nothing to be read.
  string single_string;
  if (max_readable_size && !data->ReadStringWithSizeFromData(&single_string))
    return false;
  dest->set_value(single_string);
  dest->set_value_md5_prefix(Md5Prefix(single_string));
  return true;
}

bool PerfReader::ReadRepeatedStringMetadata(
    DataReader* data, size_t max_readable_size,
    RepeatedPtrField<StringAndMd5sumPrefix>* dest_array,
    StringAndMd5sumPrefix* dest_single) const {
  num_string_data_type count = 1;
  if (!data->ReadUint32(&count)) {
    LOG(ERROR) << "Error reading string count.";
    return false;
  }
  size_t size_read = sizeof(count);

  string full_string;
  while (count-- > 0 && size_read < max_readable_size) {
    StringAndMd5sumPrefix* new_entry = dest_array->Add();
    size_t offset = data->Tell();
    if (!ReadSingleStringMetadata(data, max_readable_size - size_read,
                                  new_entry)) {
      return false;
    }

    if (!full_string.empty()) full_string += " ";
    full_string += new_entry->value();

    size_read += data->Tell() - offset;
  }

  dest_single->set_value(full_string);
  dest_single->set_value_md5_prefix(Md5Prefix(full_string));
  return true;
}

bool PerfReader::ReadUint32Metadata(DataReader* data, u32 type, size_t size) {
  PerfUint32Metadata uint32_data;
  uint32_data.type = type;

  while (size > 0) {
    uint32_t item;
    if (!data->ReadUint32(&item)) {
      LOG(ERROR) << "Error reading uint32 metadata";
      return false;
    }

    uint32_data.data.push_back(item);
    size -= sizeof(item);
  }

  serializer_.SerializeSingleUint32Metadata(uint32_data,
                                            proto_->add_uint32_metadata());
  return true;
}

bool PerfReader::ReadUint64Metadata(DataReader* data, u32 type, size_t size) {
  PerfUint64Metadata uint64_data;
  uint64_data.type = type;

  while (size > 0) {
    uint64_t item;
    if (!data->ReadUint64(&item)) {
      LOG(ERROR) << "Error reading uint64 metadata";
      return false;
    }

    uint64_data.data.push_back(item);
    size -= sizeof(item);
  }

  serializer_.SerializeSingleUint64Metadata(uint64_data,
                                            proto_->add_uint64_metadata());
  return true;
}
bool PerfReader::ReadEventDescMetadata(DataReader* data) {
  // Structure:
  // u32 nr_events
  // u32 sizeof(perf_event_attr)
  // foreach event (nr_events):
  //   struct perf_event_attr
  //   u32 nr_ids
  //   event name (len & string, 64-bit padded)
  //   u64 ids[nr_ids]

  u32 nr_events;
  if (!data->ReadUint32(&nr_events)) {
    LOG(ERROR) << "Error reading event_desc nr_events.";
    return false;
  }

  u32 attr_size;
  if (!data->ReadUint32(&attr_size)) {
    LOG(ERROR) << "Error reading event_desc attr_size.";
    return false;
  }

  file_attrs_seen_.clear();
  proto_->clear_file_attrs();
  proto_->mutable_file_attrs()->Reserve(nr_events);

  for (u32 i = 0; i < nr_events; i++) {
    PerfFileAttr attr;
    if (!ReadEventAttr(data, &attr.attr)) return false;

    u32 nr_ids;
    if (!data->ReadUint32(&nr_ids)) {
      LOG(ERROR) << "Error reading event_desc nr_ids.";
      return false;
    }

    if (!data->ReadStringWithSizeFromData(&attr.name)) return false;
    std::vector<u64>& ids = attr.ids;
    ids.resize(nr_ids);
    for (u64& id : ids) {
      if (!data->ReadUint64(&id)) {
        LOG(ERROR) << "Error reading ID value for attr #" << i;
        return false;
      }
    }
    AddPerfFileAttr(attr);
    // The EVENT_DESC metadata is the newer replacement for the older event type
    // fields. In the protobuf, both types of data are stored in the
    // |event_types| field.
    serializer_.SerializePerfEventType(attr, proto_->add_event_types());
  }
  return true;
}

bool PerfReader::ReadCPUTopologyMetadata(DataReader* data) {
  num_siblings_type num_core_siblings;
  if (!data->ReadUint32(&num_core_siblings)) {
    LOG(ERROR) << "Error reading num core siblings.";
    return false;
  }

  PerfCPUTopologyMetadata cpu_topology;
  cpu_topology.core_siblings.resize(num_core_siblings);
  for (size_t i = 0; i < num_core_siblings; ++i) {
    if (!data->ReadStringWithSizeFromData(&cpu_topology.core_siblings[i]))
      return false;
  }

  num_siblings_type num_thread_siblings;
  if (!data->ReadUint32(&num_thread_siblings)) {
    LOG(ERROR) << "Error reading num core siblings.";
    return false;
  }

  cpu_topology.thread_siblings.resize(num_thread_siblings);
  for (size_t i = 0; i < num_thread_siblings; ++i) {
    if (!data->ReadStringWithSizeFromData(&cpu_topology.thread_siblings[i]))
      return false;
  }

  serializer_.SerializeCPUTopologyMetadata(cpu_topology,
                                           proto_->mutable_cpu_topology());
  return true;
}

bool PerfReader::ReadNUMATopologyMetadata(DataReader* data) {
  numa_topology_num_nodes_type num_nodes;
  if (!data->ReadUint32(&num_nodes)) {
    LOG(ERROR) << "Error reading NUMA topology num nodes.";
    return false;
  }

  for (size_t i = 0; i < num_nodes; ++i) {
    PerfNodeTopologyMetadata node;
    if (!data->ReadUint32(&node.id) || !data->ReadUint64(&node.total_memory) ||
        !data->ReadUint64(&node.free_memory) ||
        !data->ReadStringWithSizeFromData(&node.cpu_list)) {
      LOG(ERROR) << "Error reading NUMA topology info for node #" << i;
      return false;
    }
    serializer_.SerializeNodeTopologyMetadata(node,
                                              proto_->add_numa_topology());
  }
  return true;
}

bool PerfReader::ReadPMUMappingsMetadata(DataReader* data, size_t size) {
  pmu_mappings_num_mappings_type num_mappings;
  auto begin_offset = data->Tell();
  if (!data->ReadUint32(&num_mappings)) {
    LOG(ERROR) << "Error reading the number of PMU mappings.";
    return false;
  }

  // Check size of the data read in addition to the iteration based on the
  // number of PMU mappings because the number of pmu mappings is always zero
  // in piped perf.data file.
  //
  // The number of PMU mappings is initialized to zero and after all the
  // mappings are wirtten to the perf.data files, this value is set to the
  // number of PMU mappings written. This logic doesn't work in pipe mode. So,
  // the number of PMU mappings is always zero.
  // Fix to write the number of PMU mappings before writing the actual PMU
  // mappings landed upstream in 4.14. But the check for size is required as
  // long as there are machines with older version of perf.
  for (u32 i = 0; i < num_mappings || data->Tell() - begin_offset < size; ++i) {
    PerfPMUMappingsMetadata mapping;
    if (!data->ReadUint32(&mapping.type) ||
        !data->ReadStringWithSizeFromData(&mapping.name)) {
      LOG(ERROR) << "Error reading PMU mapping info for mapping #" << i;
      return false;
    }
    serializer_.SerializePMUMappingsMetadata(mapping,
                                             proto_->add_pmu_mappings());
  }
  if (data->Tell() - begin_offset != size) {
    LOG(ERROR) << "Size from the header doesn't match the read size";
    return false;
  }
  return true;
}

bool PerfReader::ReadGroupDescMetadata(DataReader* data) {
  group_desc_num_groups_type num_groups;
  if (!data->ReadUint32(&num_groups)) {
    LOG(ERROR) << "Error reading group desc num groups.";
    return false;
  }

  for (u32 i = 0; i < num_groups; ++i) {
    PerfGroupDescMetadata group;
    if (!data->ReadStringWithSizeFromData(&group.name) ||
        !data->ReadUint32(&group.leader_idx) ||
        !data->ReadUint32(&group.num_members)) {
      LOG(ERROR) << "Error reading group desc info for group #" << i;
      return false;
    }
    serializer_.SerializeGroupDescMetadata(group, proto_->add_group_desc());
  }
  return true;
}
bool PerfReader::ReadTracingMetadata(DataReader* data, size_t size) {
  std::vector<char> tracing_data(size);
  if (!data->ReadDataValue(tracing_data.size(), "tracing_data",
                           tracing_data.data())) {
    return false;
  }
  serializer_.SerializeTracingMetadata(tracing_data, proto_);
  return true;
}

bool PerfReader::ReadFileData(DataReader* data) {
  // Make sure sections are within the size of the file. This check prevents
  // more obscure messages later when attempting to read from one of these
  // sections.
  if (header_.attrs.offset + header_.attrs.size > data->size()) {
    LOG(ERROR) << "Header says attrs section ends at "
               << header_.attrs.offset + header_.attrs.size
               << " bytes, which is larger than perf data size of "
               << data->size() << " bytes.";
    return false;
  }
  if (header_.data.offset + header_.data.size > data->size()) {
    LOG(ERROR) << "Header says data section ends at "
               << header_.data.offset + header_.data.size
               << " bytes, which is larger than perf data size of "
               << data->size() << " bytes.";
    return false;
  }
  if (header_.event_types.offset + header_.event_types.size > data->size()) {
    LOG(ERROR) << "Header says event_types section ends at "
               << header_.event_types.offset + header_.event_types.size
               << " bytes, which is larger than perf data size of "
               << data->size() << " bytes.";
    return false;
  }

  if (!get_metadata_mask_bit(HEADER_EVENT_DESC)) {
    // Prefer to read attrs and event names from HEADER_EVENT_DESC metadata if
    // available. event_types section of perf.data is obsolete, but use it as
    // a fallback:
    if (!(ReadAttrsSection(data) && ReadEventTypesSection(data))) return false;
  }

  if (!(ReadMetadata(data) && ReadDataSection(data))) return false;

  // We can construct HEADER_EVENT_DESC from attrs and event types.
  // NB: Can't set this before ReadMetadata(), or it may misread the metadata.
  if (!event_types().empty()) set_metadata_mask_bit(HEADER_EVENT_DESC);

  return true;
}

bool PerfReader::ReadPipedData(DataReader* data) {
  // The piped data comes right after the file header.
  CHECK_EQ(piped_header_.size, data->Tell());
  bool result = true;
  int num_event_types = 0;

  CheckNoEventHeaderPadding();

  while (result && data->Tell() < data->size()) {
    perf_event_header header;
    if (!ReadPerfEventHeader(data, &header)) {
      LOG(ERROR) << "Error reading event header.";
      break;
    }

    if (header.size == 0) {
      // Avoid an infinite loop.
      LOG(ERROR) << "Event size is zero. Type: " << header.type;
      return false;
    }

    // Compute the size of the post-header part of the event data.
    size_t size_without_header = header.size - sizeof(header);

    bool isHeaderEventType = [&] {
      switch (header.type) {
        case PERF_RECORD_HEADER_ATTR:
        case PERF_RECORD_HEADER_EVENT_TYPE:
        case PERF_RECORD_HEADER_TRACING_DATA:
        case PERF_RECORD_HEADER_BUILD_ID:
          return true;
        default:
          return false;
      }
    }();

    if (!isHeaderEventType) {
      // Allocate space for an event struct based on the size in the header.
      // Don't blindly allocate the entire event_t because it is a
      // variable-sized type that may include data beyond what's nominally
      // declared in its definition.
      malloced_unique_ptr<event_t> event(CallocMemoryForEvent(header.size));
      event->header = header;

      // Read the rest of the event data.
      if (!data->ReadDataValue(size_without_header, "rest of piped event",
                               &event->header + 1)) {
        break;
      }
      MaybeSwapEventFields(event.get(), data->is_cross_endian());

      // Serialize the event to protobuf form.
      PerfEvent* proto_event = proto_->add_events();
      if (!serializer_.SerializeEvent(event, proto_event)) return false;

      if (proto_event->header().type() == PERF_RECORD_AUXTRACE) {
        if (!ReadAuxtraceTraceData(data, proto_event)) return false;
      }
      continue;
    }

    result = [&] {
      switch (header.type) {
        case PERF_RECORD_HEADER_ATTR:
          return ReadAttrEventBlock(data, size_without_header);
        case PERF_RECORD_HEADER_EVENT_TYPE:
          return ReadEventType(data, num_event_types++, header.size);
        case PERF_RECORD_HEADER_TRACING_DATA:
          set_metadata_mask_bit(HEADER_TRACING_DATA);
          {
            // TRACING_DATA's header.size is a lie. It is the size of only the
            // event struct. The size of the data is in the event struct, and
            // followed immediately by the tracing header data.
            decltype(tracing_data_event::size) size = 0;
            if (!data->ReadUint32(&size)) {
              LOG(ERROR) << "Error reading tracing data size.";
              return false;
            }
            return ReadTracingMetadata(data, size);
          }
        case PERF_RECORD_HEADER_BUILD_ID:
          set_metadata_mask_bit(HEADER_BUILD_ID);
          return ReadBuildIDMetadataWithoutHeader(data, header);
        default:
          // For unsupported event types, log a warning only if the type is an
          // unknown type.
          if (header.type < PERF_RECORD_USER_TYPE_START ||
              header.type >= PERF_RECORD_HEADER_MAX) {
            LOG(WARNING) << "Unknown event type: " << header.type;
          }
          // Skip over the data in this event.
          data->SeekSet(data->Tell() + size_without_header);
          return true;
      }
    }();
  }

  if (!result) return false;

  // The PERF_RECORD_HEADER_EVENT_TYPE events are obsolete, but if present
  // and PERF_RECORD_HEADER_EVENT_DESC metadata events are not, we should use
  // them. Otherwise, we should use prefer the _EVENT_DESC data.
  if (!get_metadata_mask_bit(HEADER_EVENT_DESC) &&
      num_event_types == proto_->file_attrs().size()) {
    // We can construct HEADER_EVENT_DESC:
    set_metadata_mask_bit(HEADER_EVENT_DESC);
  }

  return result;
}

bool PerfReader::ReadAuxtraceTraceData(DataReader* data,
                                       PerfEvent* proto_event) {
  std::vector<char> trace_data(proto_event->auxtrace_event().size());
  if (!data->ReadDataValue(trace_data.size(),
                           "trace date from PERF_RECORD_AUXTRACE event",
                           trace_data.data())) {
    return false;
  }
  if (data->is_cross_endian()) {
    LOG(ERROR) << "Cannot byteswap trace data from PERF_RECORD_AUXTRACE";
  }
  if (!serializer_.SerializeAuxtraceEventTraceData(
          trace_data, proto_event->mutable_auxtrace_event())) {
    return false;
  }
  return true;
}

bool PerfReader::WriteHeader(const struct perf_file_header& header,
                             DataWriter* data) const {
  CheckNoEventHeaderPadding();
  size_t size = sizeof(header);
  return data->WriteDataValue(&header, size, "file header");
}

bool PerfReader::WriteAttrs(const struct perf_file_header& header,
                            DataWriter* data) const {
  CheckNoPerfEventAttrPadding();
  const size_t id_offset = header.size;
  CHECK_EQ(id_offset, data->Tell());

  std::vector<struct perf_file_section> id_sections;
  id_sections.reserve(attrs().size());
  for (const auto& attr : proto_->file_attrs()) {
    size_t section_size =
        attr.ids_size() * sizeof(decltype(PerfFileAttr::ids)::value_type);
    id_sections.push_back(perf_file_section{data->Tell(), section_size});
    for (const uint64_t& id : attr.ids()) {
      if (!data->WriteDataValue(&id, sizeof(id), "ID info")) return false;
    }
  }

  CHECK_EQ(header.attrs.offset, data->Tell());
  for (int i = 0; i < attrs().size(); i++) {
    const struct perf_file_section& id_section = id_sections[i];
    PerfFileAttr attr;
    serializer_.DeserializePerfFileAttr(proto_->file_attrs(i), &attr);
    if (!data->WriteDataValue(&attr.attr, sizeof(attr.attr), "attribute") ||
        !data->WriteDataValue(&id_section, sizeof(id_section), "ID section")) {
      return false;
    }
  }
  return true;
}

bool PerfReader::WriteData(const struct perf_file_header& header,
                           DataWriter* data) const {
  // No need to CHECK anything if no event data is being written.
  if (proto_->events().empty()) return true;

  CHECK(serializer_.SampleInfoReaderAvailable());
  CHECK_EQ(header.data.offset, data->Tell());
  for (const PerfEvent& proto_event : proto_->events()) {
    malloced_unique_ptr<event_t> event;
    // The nominal size given by |proto_event| may not be correct, as the
    // contents may have changed since the PerfEvent was created. Use the size
    // in the event_t returned by PerfSerializer::DeserializeEvent().
    if (!serializer_.DeserializeEvent(proto_event, &event) ||
        !data->WriteDataValue(event.get(), event->header.size, "event data")) {
      return false;
    }
    // PERF_RECORD_AUXTRACE contains trace data that is written after writing
    // the actual event data.
    if (proto_event.header().type() == PERF_RECORD_AUXTRACE &&
        proto_event.auxtrace_event().size() > 0) {
      std::vector<char> trace_data;
      if (!serializer_.DeserializeAuxtraceEventTraceData(
              proto_event.auxtrace_event(), &trace_data) ||
          !data->WriteDataValue(reinterpret_cast<void*>(trace_data.data()),
                                trace_data.size(),
                                "trace data from PERF_RECORD_AUXTRACE event")) {
        return false;
      }
    }
  }
  return true;
}

bool PerfReader::WriteMetadata(const struct perf_file_header& header,
                               DataWriter* data) const {
  const size_t header_offset = header.data.offset + header.data.size;
  CHECK_EQ(header_offset, data->Tell());

  // There is one header for each feature pointing to the metadata for that
  // feature. If a feature has no metadata, the size field is zero.
  const size_t headers_size =
      GetNumSupportedMetadata() * sizeof(perf_file_section);
  const size_t metadata_offset = header_offset + headers_size;
  data->SeekSet(metadata_offset);

  // Record the new metadata offsets and sizes in this vector of info entries.
  std::vector<struct perf_file_section> metadata_sections;
  metadata_sections.reserve(GetNumSupportedMetadata());

  // For less verbose access to string metadata fields.
  const auto& string_metadata = proto_->string_metadata();

  for (u32 type = HEADER_FIRST_FEATURE; type != HEADER_LAST_FEATURE; ++type) {
    if ((header.adds_features[0] & (1 << type)) == 0) continue;

    struct perf_file_section header_entry;
    header_entry.offset = data->Tell();
    // Write actual metadata to address metadata_offset
    switch (type) {
      case HEADER_TRACING_DATA:
        if (!data->WriteDataValue(tracing_data().data(), tracing_data().size(),
                                  "tracing data")) {
          return false;
        }
        break;
      case HEADER_BUILD_ID:
        if (!WriteBuildIDMetadata(type, data)) return false;
        break;
      case HEADER_HOSTNAME:
        if (!WriteSingleStringMetadata(string_metadata.hostname(), data))
          return false;
        break;
      case HEADER_OSRELEASE:
        if (!WriteSingleStringMetadata(string_metadata.kernel_version(), data))
          return false;
        break;
      case HEADER_VERSION:
        if (!WriteSingleStringMetadata(string_metadata.perf_version(), data))
          return false;
        break;
      case HEADER_ARCH:
        if (!WriteSingleStringMetadata(string_metadata.architecture(), data))
          return false;
        break;
      case HEADER_CPUDESC:
        if (!WriteSingleStringMetadata(string_metadata.cpu_description(), data))
          return false;
        break;
      case HEADER_CPUID:
        if (!WriteSingleStringMetadata(string_metadata.cpu_id(), data))
          return false;
        break;
      case HEADER_CMDLINE:
        if (!WriteRepeatedStringMetadata(
                string_metadata.perf_command_line_token(), data)) {
          return false;
        }
        break;
      case HEADER_NRCPUS:
        if (!WriteUint32Metadata(type, data)) return false;
        break;
      case HEADER_TOTAL_MEM:
        if (!WriteUint64Metadata(type, data)) return false;
        break;
      case HEADER_EVENT_DESC:
        if (!WriteEventDescMetadata(data)) return false;
        break;
      case HEADER_CPU_TOPOLOGY:
        if (!WriteCPUTopologyMetadata(data)) return false;
        break;
      case HEADER_NUMA_TOPOLOGY:
        if (!WriteNUMATopologyMetadata(data)) return false;
        break;
      case HEADER_BRANCH_STACK:
        break;
      case HEADER_PMU_MAPPINGS:
        if (!WritePMUMappingsMetadata(data)) return false;
        break;
      case HEADER_GROUP_DESC:
        if (!WriteGroupDescMetadata(data)) return false;
        break;
      default:
        LOG(ERROR) << "Unsupported metadata type: " << type;
        return false;
    }

    // Compute the size of the metadata that was just written. This is reflected
    // in how much the data write pointer has moved.
    header_entry.size = data->Tell() - header_entry.offset;
    metadata_sections.push_back(header_entry);
  }
  // Make sure we have recorded the right number of entries.
  CHECK_EQ(GetNumSupportedMetadata(), metadata_sections.size());

  // Now write the metadata offset and size info back to the metadata headers.
  size_t old_offset = data->Tell();
  data->SeekSet(header_offset);
  if (!data->WriteDataValue(metadata_sections.data(), headers_size,
                            "metadata section info")) {
    return false;
  }
  // Make sure the write pointer now points to the end of the metadata headers
  // and hence the beginning of the actual metadata.
  CHECK_EQ(metadata_offset, data->Tell());
  data->SeekSet(old_offset);

  return true;
}

bool PerfReader::WriteBuildIDMetadata(u32 type, DataWriter* data) const {
  CheckNoBuildIDEventPadding();
  for (const auto& build_id : proto_->build_ids()) {
    malloced_unique_ptr<build_id_event> event;
    if (!serializer_.DeserializeBuildIDEvent(build_id, &event)) {
      LOG(ERROR) << "Could not deserialize build ID event with build ID "
                 << RawDataToHexString(build_id.build_id_hash());
      return false;
    }
    if (!data->WriteDataValue(event.get(), event->header.size,
                              "Build ID metadata")) {
      return false;
    }
  }
  return true;
}

bool PerfReader::WriteSingleStringMetadata(const StringAndMd5sumPrefix& src,
                                           DataWriter* data) const {
  return data->WriteStringWithSizeToData(src.value());
}

bool PerfReader::WriteRepeatedStringMetadata(
    const RepeatedPtrField<StringAndMd5sumPrefix>& src_array,
    DataWriter* data) const {
  num_string_data_type num_strings = src_array.size();
  if (!data->WriteDataValue(&num_strings, sizeof(num_strings),
                            "number of string metadata")) {
    return false;
  }
  for (const auto& src_entry : src_array) {
    if (!data->WriteStringWithSizeToData(src_entry.value())) return false;
  }
  return true;
}

bool PerfReader::WriteUint32Metadata(u32 type, DataWriter* data) const {
  for (const auto& metadata : proto_->uint32_metadata()) {
    if (metadata.type() != type) continue;
    PerfUint32Metadata local_metadata;
    serializer_.DeserializeSingleUint32Metadata(metadata, &local_metadata);
    const std::vector<uint32_t>& raw_data = local_metadata.data;
    return data->WriteDataValue(raw_data.data(),
                                raw_data.size() * sizeof(uint32_t),
                                "uint32_t metadata");
  }
  LOG(ERROR) << "Uint32 metadata of type " << type << " not present";
  return false;
}

bool PerfReader::WriteUint64Metadata(u32 type, DataWriter* data) const {
  for (const auto& metadata : proto_->uint64_metadata()) {
    if (metadata.type() != type) continue;
    PerfUint64Metadata local_metadata;
    serializer_.DeserializeSingleUint64Metadata(metadata, &local_metadata);
    const std::vector<uint64_t>& raw_data = local_metadata.data;
    return data->WriteDataValue(raw_data.data(),
                                raw_data.size() * sizeof(uint64_t),
                                "uint32_t metadata");
  }
  LOG(ERROR) << "Uint64 metadata of type " << type << " not present";
  return false;
}

bool PerfReader::WriteEventDescMetadata(DataWriter* data) const {
  CheckNoPerfEventAttrPadding();

  if (attrs().size() > event_types().size()) {
    LOG(ERROR) << "Number of attrs (" << attrs().size() << ") cannot exceed "
               << "number of event types (" << event_types().size() << ")";
    return false;
  }

  event_desc_num_events num_events = proto_->file_attrs().size();
  if (!data->WriteDataValue(&num_events, sizeof(num_events),
                            "event_desc num_events")) {
    return false;
  }
  event_desc_attr_size attr_size = sizeof(perf_event_attr);
  if (!data->WriteDataValue(&attr_size, sizeof(attr_size),
                            "event_desc attr_size")) {
    return false;
  }

  for (int i = 0; i < attrs().size(); ++i) {
    const auto& stored_attr = attrs().Get(i);
    PerfFileAttr attr;
    serializer_.DeserializePerfFileAttr(stored_attr, &attr);
    if (!serializer_.DeserializePerfEventType(proto_->event_types(i), &attr))
      return false;

    if (!data->WriteDataValue(&attr.attr, sizeof(attr.attr),
                              "event_desc attribute")) {
      return false;
    }

    event_desc_num_unique_ids num_ids = attr.ids.size();
    if (!data->WriteDataValue(&num_ids, sizeof(num_ids),
                              "event_desc num_unique_ids")) {
      return false;
    }

    if (!data->WriteStringWithSizeToData(attr.name)) return false;

    if (!data->WriteDataValue(attr.ids.data(), num_ids * sizeof(attr.ids[0]),
                              "event_desc unique_ids")) {
      return false;
    }
  }
  return true;
}

bool PerfReader::WriteCPUTopologyMetadata(DataWriter* data) const {
  PerfCPUTopologyMetadata cpu_topology;
  serializer_.DeserializeCPUTopologyMetadata(proto_->cpu_topology(),
                                             &cpu_topology);

  std::vector<string>& cores = cpu_topology.core_siblings;
  num_siblings_type num_cores = cores.size();
  if (!data->WriteDataValue(&num_cores, sizeof(num_cores), "num cores"))
    return false;
  for (string& core_name : cores) {
    if (!data->WriteStringWithSizeToData(core_name)) return false;
  }

  std::vector<string>& threads = cpu_topology.thread_siblings;
  num_siblings_type num_threads = threads.size();
  if (!data->WriteDataValue(&num_threads, sizeof(num_threads), "num threads"))
    return false;
  for (string& thread_name : threads) {
    if (!data->WriteStringWithSizeToData(thread_name)) return false;
  }

  return true;
}

bool PerfReader::WriteNUMATopologyMetadata(DataWriter* data) const {
  numa_topology_num_nodes_type num_nodes = proto_->numa_topology().size();
  if (!data->WriteDataValue(&num_nodes, sizeof(num_nodes), "num nodes"))
    return false;

  for (const auto& node_proto : proto_->numa_topology()) {
    PerfNodeTopologyMetadata node;
    serializer_.DeserializeNodeTopologyMetadata(node_proto, &node);

    if (!data->WriteDataValue(&node.id, sizeof(node.id), "node id") ||
        !data->WriteDataValue(&node.total_memory, sizeof(node.total_memory),
                              "node total memory") ||
        !data->WriteDataValue(&node.free_memory, sizeof(node.free_memory),
                              "node free memory") ||
        !data->WriteStringWithSizeToData(node.cpu_list)) {
      return false;
    }
  }
  return true;
}

bool PerfReader::WritePMUMappingsMetadata(DataWriter* data) const {
  pmu_mappings_num_mappings_type num_mappings = proto_->pmu_mappings().size();
  if (!data->WriteDataValue(&num_mappings, sizeof(num_mappings),
                            "num mappings"))
    return false;

  for (const auto& mapping_proto : proto_->pmu_mappings()) {
    PerfPMUMappingsMetadata mapping;
    serializer_.DeserializePMUMappingsMetadata(mapping_proto, &mapping);

    if (!data->WriteDataValue(&mapping.type, sizeof(mapping.type),
                              "mapping type") ||
        !data->WriteStringWithSizeToData(mapping.name)) {
      return false;
    }
  }
  return true;
}

bool PerfReader::WriteGroupDescMetadata(DataWriter* data) const {
  group_desc_num_groups_type num_groups = proto_->group_desc().size();
  if (!data->WriteDataValue(&num_groups, sizeof(num_groups), "num groups"))
    return false;

  for (const auto& group_proto : proto_->group_desc()) {
    PerfGroupDescMetadata group;
    serializer_.DeserializeGroupDescMetadata(group_proto, &group);

    if (!data->WriteStringWithSizeToData(group.name) ||
        !data->WriteDataValue(&group.leader_idx, sizeof(group.leader_idx),
                              "group leader index") ||
        !data->WriteDataValue(&group.num_members, sizeof(group.num_members),
                              "group num members")) {
      return false;
    }
  }
  return true;
}

bool PerfReader::ReadAttrEventBlock(DataReader* data, size_t size) {
  const size_t initial_offset = data->Tell();
  PerfFileAttr attr;
  if (!ReadEventAttr(data, &attr.attr)) return false;

  // attr.attr.size has been upgraded to the current size of perf_event_attr.
  const size_t actual_attr_size = data->Tell() - initial_offset;

  const size_t num_ids =
      (size - actual_attr_size) / sizeof(decltype(attr.ids)::value_type);
  if (!ReadUniqueIDs(data, num_ids, &attr.ids)) return false;

  // Event types are found many times in the perf data file.
  // Only add this event type if it is not already present.
  if (!attr.ids.empty() &&
      file_attrs_seen_.find(attr.ids[0]) != file_attrs_seen_.end()) {
    return true;
  }

  AddPerfFileAttr(attr);
  return true;
}

void PerfReader::MaybeSwapEventFields(event_t* event, bool is_cross_endian) {
  if (!is_cross_endian) return;
  uint32_t type = event->header.type;
  switch (type) {
    case PERF_RECORD_SAMPLE:
      break;
    case PERF_RECORD_MMAP:
      ByteSwap(&event->mmap.pid);
      ByteSwap(&event->mmap.tid);
      ByteSwap(&event->mmap.start);
      ByteSwap(&event->mmap.len);
      ByteSwap(&event->mmap.pgoff);
      break;
    case PERF_RECORD_MMAP2:
      ByteSwap(&event->mmap2.pid);
      ByteSwap(&event->mmap2.tid);
      ByteSwap(&event->mmap2.start);
      ByteSwap(&event->mmap2.len);
      ByteSwap(&event->mmap2.pgoff);
      ByteSwap(&event->mmap2.maj);
      ByteSwap(&event->mmap2.min);
      ByteSwap(&event->mmap2.ino);
      ByteSwap(&event->mmap2.ino_generation);
      ByteSwap(&event->mmap2.prot);
      ByteSwap(&event->mmap2.flags);
      break;
    case PERF_RECORD_FORK:
    case PERF_RECORD_EXIT:
      ByteSwap(&event->fork.pid);
      ByteSwap(&event->fork.tid);
      ByteSwap(&event->fork.ppid);
      ByteSwap(&event->fork.ptid);
      ByteSwap(&event->fork.time);
      break;
    case PERF_RECORD_COMM:
      ByteSwap(&event->comm.pid);
      ByteSwap(&event->comm.tid);
      break;
    case PERF_RECORD_LOST:
      ByteSwap(&event->lost.id);
      ByteSwap(&event->lost.lost);
      break;
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
      ByteSwap(&event->throttle.time);
      ByteSwap(&event->throttle.id);
      ByteSwap(&event->throttle.stream_id);
      break;
    case PERF_RECORD_READ:
      ByteSwap(&event->read.pid);
      ByteSwap(&event->read.tid);
      ByteSwap(&event->read.value);
      ByteSwap(&event->read.time_enabled);
      ByteSwap(&event->read.time_running);
      ByteSwap(&event->read.id);
      break;
    case PERF_RECORD_AUX:
      ByteSwap(&event->aux.aux_offset);
      ByteSwap(&event->aux.aux_size);
      ByteSwap(&event->aux.flags);
      break;
    case PERF_RECORD_AUXTRACE:
      ByteSwap(&event->auxtrace.size);
      ByteSwap(&event->auxtrace.offset);
      ByteSwap(&event->auxtrace.reference);
      ByteSwap(&event->auxtrace.idx);
      ByteSwap(&event->auxtrace.tid);
      ByteSwap(&event->auxtrace.cpu);
      break;
    default:
      LOG(FATAL) << "Unknown event type: " << type;
  }

  // Do not swap the sample info fields that are not explicitly listed in the
  // struct definition of each event type. Leave that up to SampleInfoReader
  // within |serializer_|.
}

size_t PerfReader::GetNumSupportedMetadata() const {
  return GetNumBits(metadata_mask() & kSupportedMetadataMask);
}

size_t PerfReader::GetEventDescMetadataSize() const {
  if (attrs().size() > event_types().size()) {
    LOG(ERROR) << "Number of attrs (" << attrs().size() << ") cannot exceed "
               << "number of event types (" << event_types().size() << ")";
    return 0;
  }

  size_t size = 0;
  if (get_metadata_mask_bit(HEADER_EVENT_DESC)) {
    size += sizeof(event_desc_num_events) + sizeof(event_desc_attr_size);
    for (int i = 0; i < attrs().size(); ++i) {
      size += sizeof(perf_event_attr);
      size += sizeof(event_desc_num_unique_ids);
      size += ExpectedStorageSizeOf(event_types().Get(i).name());
      size += attrs().Get(i).ids_size() *
              sizeof(decltype(PerfFileAttr::ids)::value_type);
    }
  }
  return size;
}

size_t PerfReader::GetBuildIDMetadataSize() const {
  size_t size = 0;
  for (const auto& build_id : proto_->build_ids()) {
    size += sizeof(build_id_event) +
            GetUint64AlignedStringLength(build_id.filename());
  }
  return size;
}

size_t PerfReader::GetStringMetadataSize() const {
  size_t size = 0;
  if (string_metadata().has_hostname())
    size += ExpectedStorageSizeOf(string_metadata().hostname().value());
  if (string_metadata().has_kernel_version())
    size += ExpectedStorageSizeOf(string_metadata().kernel_version().value());
  if (string_metadata().has_perf_version())
    size += ExpectedStorageSizeOf(string_metadata().perf_version().value());
  if (string_metadata().has_architecture())
    size += ExpectedStorageSizeOf(string_metadata().architecture().value());
  if (string_metadata().has_cpu_description())
    size += ExpectedStorageSizeOf(string_metadata().cpu_description().value());
  if (string_metadata().has_cpu_id())
    size += ExpectedStorageSizeOf(string_metadata().cpu_id().value());

  if (!string_metadata().perf_command_line_token().empty()) {
    size += sizeof(num_string_data_type);
    for (const auto& token : string_metadata().perf_command_line_token())
      size += ExpectedStorageSizeOf(token.value());
  }
  return size;
}

size_t PerfReader::GetUint32MetadataSize() const {
  size_t size = 0;
  for (const auto& metadata : proto_->uint32_metadata())
    size += metadata.data().size() * sizeof(uint32_t);
  return size;
}

size_t PerfReader::GetUint64MetadataSize() const {
  size_t size = 0;
  for (const auto& metadata : proto_->uint64_metadata())
    size += metadata.data().size() * sizeof(uint64_t);
  return size;
}

size_t PerfReader::GetCPUTopologyMetadataSize() const {
  // Core siblings.
  size_t size = sizeof(num_siblings_type);
  for (const string& core_sibling : proto_->cpu_topology().core_siblings())
    size += ExpectedStorageSizeOf(core_sibling);

  // Thread siblings.
  size += sizeof(num_siblings_type);
  for (const string& thread_sibling : proto_->cpu_topology().thread_siblings())
    size += ExpectedStorageSizeOf(thread_sibling);

  return size;
}

size_t PerfReader::GetNUMATopologyMetadataSize() const {
  size_t size = sizeof(numa_topology_num_nodes_type);
  for (const auto& node : proto_->numa_topology()) {
    size += sizeof(node.id());
    size += sizeof(node.total_memory()) + sizeof(node.free_memory());
    size += ExpectedStorageSizeOf(node.cpu_list());
  }
  return size;
}

size_t PerfReader::GetPMUMappingsMetadataSize() const {
  size_t size = sizeof(pmu_mappings_num_mappings_type);
  for (const auto& node : proto_->pmu_mappings()) {
    size += sizeof(node.type());
    size += ExpectedStorageSizeOf(node.name());
  }
  return size;
}

size_t PerfReader::GetGroupDescMetadataSize() const {
  size_t size = sizeof(group_desc_num_groups_type);
  for (const auto& group : proto_->group_desc()) {
    size += ExpectedStorageSizeOf(group.name());
    size += sizeof(group.leader_idx());
    size += sizeof(group.num_members());
  }
  return size;
}

bool PerfReader::NeedsNumberOfStringData(u32 type) const {
  return type == HEADER_CMDLINE;
}

bool PerfReader::LocalizeMMapFilenames(
    const std::map<string, string>& filename_map) {
  CHECK(serializer_.SampleInfoReaderAvailable());

  // Search for mmap/mmap2 events for which the filename needs to be updated.
  for (PerfEvent& event : *proto_->mutable_events()) {
    if (event.header().type() != PERF_RECORD_MMAP &&
        event.header().type() != PERF_RECORD_MMAP2) {
      continue;
    }
    const string& filename = event.mmap_event().filename();
    const auto it = filename_map.find(filename);
    if (it == filename_map.end())  // not found
      continue;

    const string& new_filename = it->second;
    size_t old_len = GetUint64AlignedStringLength(filename);
    size_t new_len = GetUint64AlignedStringLength(new_filename);
    size_t new_size = event.header().size() - old_len + new_len;

    event.mutable_mmap_event()->set_filename(new_filename);
    event.mutable_header()->set_size(new_size);
  }

  return true;
}

void PerfReader::AddPerfFileAttr(const PerfFileAttr& attr) {
  serializer_.SerializePerfFileAttr(attr, proto_->add_file_attrs());

  // Generate a new SampleInfoReader with the new attr.
  serializer_.CreateSampleInfoReader(attr, is_cross_endian_);
  if (!attr.ids.empty()) {
    file_attrs_seen_.insert(attr.ids[0]);
  }
}

}  // namespace quipper
