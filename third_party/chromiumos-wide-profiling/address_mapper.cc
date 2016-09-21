// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/address_mapper.h"

#include <stdint.h>

#include <vector>

#include "base/logging.h"

namespace quipper {

bool AddressMapper::MapWithID(const uint64_t real_addr,
                              const uint64_t size,
                              const uint64_t id,
                              const uint64_t offset_base,
                              bool remove_existing_mappings) {
  MappedRange range;
  range.real_addr = real_addr;
  range.size = size;
  range.id = id;
  range.offset_base = offset_base;

  if (size == 0) {
    LOG(ERROR) << "Must allocate a nonzero-length address range.";
    return false;
  }

  // Check that this mapping does not overflow the address space.
  if (real_addr + size - 1 != UINT64_MAX && !(real_addr + size > real_addr)) {
    DumpToLog();
    LOG(ERROR) << "Address mapping at " << std::hex << real_addr
               << " with size " << std::hex << size << " overflows.";
    return false;
  }

  // Check for collision with an existing mapping.  This must be an overlap that
  // does not result in one range being completely covered by another
  MappingList::iterator iter;
  std::vector<MappingList::iterator> mappings_to_delete;
  MappingList::iterator old_range_iter = mappings_.end();
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (!iter->Intersects(range))
      continue;
    // Quit if existing ranges that collide aren't supposed to be removed.
    if (!remove_existing_mappings)
      return false;
    if (old_range_iter == mappings_.end() && iter->Covers(range)
        && iter->size > range.size) {
      old_range_iter = iter;
      continue;
    }
    mappings_to_delete.push_back(iter);
  }

  for (MappingList::iterator mapping_iter : mappings_to_delete)
    Unmap(mapping_iter);
  mappings_to_delete.clear();

  // Otherwise check for this range being covered by another range.  If that
  // happens, split or reduce the existing range to make room.
  if (old_range_iter != mappings_.end()) {
    // Make a copy of the old mapping before removing it.
    const MappedRange old_range = *old_range_iter;
    Unmap(old_range_iter);

    uint64_t gap_before = range.real_addr - old_range.real_addr;
    uint64_t gap_after = (old_range.real_addr + old_range.size) -
                         (range.real_addr + range.size);

    // If the new mapping is not aligned to a page boundary at either its start
    // or end, it will require the end of the old mapping range to be moved,
    // which is not allowed.
    if (page_alignment_) {
      if ((gap_before && GetAlignedOffset(range.real_addr)) ||
          (gap_after && GetAlignedOffset(range.real_addr + range.size))) {
        LOG(ERROR) << "Split mapping must result in page-aligned mappings.";
        return false;
      }
    }

    if (gap_before) {
      if (!MapWithID(old_range.real_addr, gap_before, old_range.id,
                     old_range.offset_base, false)) {
        LOG(ERROR) << "Could not map old range from " << std::hex
                   << old_range.real_addr << " to "
                   << old_range.real_addr + gap_before;
        return false;
      }
    }

    if (!MapWithID(range.real_addr, range.size, id, offset_base, false)) {
      LOG(ERROR) << "Could not map new range at " << std::hex << range.real_addr
                 << " to " << range.real_addr + range.size << " over old range";
      return false;
    }

    if (gap_after) {
      if (!MapWithID(range.real_addr + range.size, gap_after, old_range.id,
                     old_range.offset_base + gap_before + range.size, false)) {
        LOG(ERROR) << "Could not map old range from " << std::hex
                   << old_range.real_addr << " to "
                   << old_range.real_addr + gap_before;
        return false;
      }
    }

    return true;
  }

  // Now search for a location for the new range.  It should be in the first
  // free block in quipper space.

  uint64_t page_offset =
      page_alignment_ ? GetAlignedOffset(range.real_addr) : 0;

  // If there is no existing mapping, add it to the beginning of quipper space.
  if (mappings_.empty()) {
    range.mapped_addr = page_offset;
    range.unmapped_space_after = UINT64_MAX - range.size - page_offset;
    mappings_.push_back(range);
    return true;
  }

  // If there is space before the first mapped range in quipper space, use it.
  if (mappings_.begin()->mapped_addr >= range.size + page_offset) {
    range.mapped_addr = page_offset;
    range.unmapped_space_after =
        mappings_.begin()->mapped_addr - range.size - page_offset;
    mappings_.push_front(range);
    return true;
  }

  // Otherwise, search through the existing mappings for a free block after one
  // of them.
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    MappedRange& existing_mapping = *iter;
    if (page_alignment_) {
      uint64_t end_of_existing_mapping =
          existing_mapping.mapped_addr + existing_mapping.size;

      // Find next page boundary after end of this existing mapping.
      uint64_t existing_page_offset = GetAlignedOffset(end_of_existing_mapping);
      uint64_t next_page_boundary =
          existing_page_offset
              ? end_of_existing_mapping - existing_page_offset + page_alignment_
              : end_of_existing_mapping;
      // Compute where the new mapping would end if it were aligned to this
      // page boundary.
      uint64_t mapping_offset = GetAlignedOffset(range.real_addr);
      uint64_t end_of_new_mapping =
          next_page_boundary + mapping_offset + range.size;
      uint64_t end_of_unmapped_space_after =
          end_of_existing_mapping + existing_mapping.unmapped_space_after;

      // Check if there's enough room in the unmapped space following the
      // current existing mapping for the page-aligned mapping.
      if (end_of_new_mapping > end_of_unmapped_space_after)
        continue;

      range.mapped_addr = next_page_boundary + mapping_offset;
      range.unmapped_space_after =
          end_of_unmapped_space_after - end_of_new_mapping;
      existing_mapping.unmapped_space_after =
          range.mapped_addr - end_of_existing_mapping;
    } else {
      if (existing_mapping.unmapped_space_after < range.size)
        continue;
      // Insert the new mapping range immediately after the existing one.
      range.mapped_addr = existing_mapping.mapped_addr + existing_mapping.size;
      range.unmapped_space_after =
          existing_mapping.unmapped_space_after - range.size;
      existing_mapping.unmapped_space_after = 0;
    }

    mappings_.insert(++iter, range);
    return true;
  }

  // If it still hasn't succeeded in mapping, it means there is no free space in
  // quipper space large enough for a mapping of this size.
  DumpToLog();
  LOG(ERROR) << "Could not find space to map addr=" << std::hex << real_addr
             << " with size " << std::hex << size;
  return false;
}

void AddressMapper::DumpToLog() const {
  MappingList::const_iterator it;
  for (it = mappings_.begin(); it != mappings_.end(); ++it) {
    LOG(INFO) << " real_addr: " << std::hex << it->real_addr
              << " mapped: " << std::hex << it->mapped_addr
              << " id: " << std::hex << it->id
              << " size: " << std::hex << it->size;
  }
}

bool AddressMapper::GetMappedAddress(const uint64_t real_addr,
                                     uint64_t* mapped_addr) const {
  CHECK(mapped_addr);
  MappingList::const_iterator iter;
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (!iter->ContainsAddress(real_addr))
      continue;
    *mapped_addr = iter->mapped_addr + real_addr - iter->real_addr;
    return true;
  }
  return false;
}

bool AddressMapper::GetMappedIDAndOffset(const uint64_t real_addr,
                                         uint64_t* id,
                                         uint64_t* offset) const {
  CHECK(id);
  CHECK(offset);
  MappingList::const_iterator iter;
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (!iter->ContainsAddress(real_addr))
      continue;
    *id = iter->id;
    *offset = real_addr - iter->real_addr + iter->offset_base;
    return true;
  }
  return false;
}

uint64_t AddressMapper::GetMaxMappedLength() const {
  if (IsEmpty())
    return 0;

  uint64_t min = mappings_.begin()->mapped_addr;

  MappingList::const_iterator iter = mappings_.end();
  --iter;
  uint64_t max = iter->mapped_addr + iter->size;

  return max - min;
}

void AddressMapper::Unmap(MappingList::iterator mapping_iter) {
  // Add the freed up space to the free space counter of the previous
  // mapped region, if it exists.
  if (mapping_iter != mappings_.begin()) {
    const MappedRange& range = *mapping_iter;
    MappingList::iterator previous_range_iter = std::prev(mapping_iter);
    previous_range_iter->unmapped_space_after +=
        range.size + range.unmapped_space_after;
  }
  mappings_.erase(mapping_iter);
}

}  // namespace quipper
