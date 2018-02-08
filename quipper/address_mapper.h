// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_ADDRESS_MAPPER_H_
#define CHROMIUMOS_WIDE_PROFILING_ADDRESS_MAPPER_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <map>

namespace quipper {

class AddressMapper {
 private:
  struct MappedRange;

 public:
  AddressMapper() : page_alignment_(0) {}

  // Copy constructor: copies mappings from |source| to this AddressMapper. This
  // is useful for copying mappings from parent to child process upon fork(). It
  // is also useful to copy kernel mappings to any process that is created.
  AddressMapper(const AddressMapper& other);

  typedef std::list<MappedRange> MappingList;

  // Maps a new address range [real_addr, real_addr + length) to quipper space.
  // |id| is an identifier value to be stored along with the mapping.
  // AddressMapper does not care whether it is unique compared to all other IDs
  // passed in. That is up to the caller to keep track of.
  // |offset_base| represents the offset within the original region at which the
  // mapping begins. The original region can be much larger than the mapped
  // region.
  // e.g. Given a mapped region with base=0x4000 and size=0x2000 mapped with
  // offset_base=0x10000, then the address 0x5000 maps to an offset of 0x11000
  // (0x5000 - 0x4000 + 0x10000).
  // |remove_existing_mappings| indicates whether to remove old mappings that
  // collide with the new range in real address space, indicating it has been
  // unmapped.
  // Returns true if mapping was successful.
  bool MapWithID(const uint64_t real_addr, const uint64_t size,
                 const uint64_t id, const uint64_t offset_base,
                 bool remove_existing_mappings);

  // Looks up |real_addr| and returns the mapped address and MappingList
  // iterator.
  bool GetMappedAddressAndListIterator(const uint64_t real_addr,
                                       uint64_t* mapped_addr,
                                       MappingList::const_iterator* iter) const;

  // Uses MappingList iterator to fetch and return the mapping's ID and offset
  // from the start of the mapped space.
  // |real_addr_iter| must be valid and not at the end of the list.
  void GetMappedIDAndOffset(const uint64_t real_addr,
                            MappingList::const_iterator real_addr_iter,
                            uint64_t* id, uint64_t* offset) const;

  // Returns true if there are no mappings.
  bool IsEmpty() const { return mappings_.empty(); }

  // Returns the number of address ranges that are currently mapped.
  size_t GetNumMappedRanges() const { return mappings_.size(); }

  // Returns the maximum length of quipper space containing mapped areas.
  // There may be gaps in between blocks.
  // If the result is 2^64 (all of quipper space), this returns 0.  Call
  // IsEmpty() to distinguish this from actual emptiness.
  uint64_t GetMaxMappedLength() const;

  // Sets the page alignment size. Set to 0 to disable page alignment.
  // The alignment value must be a power of two. Any other value passed in will
  // have no effect. Changing this value in between mappings results in
  // undefined behavior.
  void set_page_alignment(uint64_t alignment) {
    // This also includes the case of 0.
    if ((alignment & (alignment - 1)) == 0) page_alignment_ = alignment;
  }

  // Dumps the state of the address mapper to logs. Useful for debugging.
  void DumpToLog() const;

 private:
  typedef std::map<uint64_t, MappingList::iterator> MappingMap;

  struct MappedRange {
    uint64_t real_addr;
    uint64_t mapped_addr;
    uint64_t size;

    uint64_t id;
    uint64_t offset_base;

    // Length of unmapped space after this range.
    uint64_t unmapped_space_after;

    // Determines if this range intersects another range in real space.
    inline bool Intersects(const MappedRange& range) const {
      return (real_addr <= range.real_addr + range.size - 1) &&
             (real_addr + size - 1 >= range.real_addr);
    }

    // Determines if this range fully covers another range in real space.
    inline bool Covers(const MappedRange& range) const {
      return (real_addr <= range.real_addr) &&
             (real_addr + size - 1 >= range.real_addr + range.size - 1);
    }

    // Determines if this range fully contains another range in real space.
    // This is different from Covers() in that the boundaries cannot overlap.
    inline bool Contains(const MappedRange& range) const {
      return (real_addr < range.real_addr) &&
             (real_addr + size - 1 > range.real_addr + range.size - 1);
    }

    // Determines if this range contains the given address |addr|.
    inline bool ContainsAddress(uint64_t addr) const {
      return (addr >= real_addr && addr <= real_addr + size - 1);
    }
  };

  // Returns an iterator to a MappedRange in |mappings_| that contains
  // |real_addr|. Returns |mappings_.end()| if no range contains |real_addr|.
  MappingList::const_iterator GetRangeContainingAddress(
      uint64_t real_addr) const;

  // Removes an existing address mapping, given by an iterator pointing to an
  // element of |mappings_|.
  void Unmap(MappingList::iterator mapping_iter);

  // Given an address, and a nonzero, power-of-two |page_alignment_| value,
  // returns the offset of the address from the start of the page it is on.
  // Equivalent to |addr % page_alignment_|. Should not be called if
  // |page_alignment_| is zero.
  uint64_t GetAlignedOffset(uint64_t addr) const {
    return addr & (page_alignment_ - 1);
  }

  // Container for all the existing mappings.
  MappingList mappings_;

  // Maps real addresses to iterators pointing to entries within |mappings_|.
  // Must maintain a 1:1 entry correspondence with |mappings_|.
  MappingMap real_addr_to_mapped_range_;

  // If set to nonzero, use this as a mapping page boundary. If a mapping does
  // not begin at a multiple of this value, the remapped address should be given
  // an offset that is the remainder.
  //
  // e.g. if alignment=0x1000 and a mapping starts at 0x520100, then the
  // remapping should treat the mapping as starting at 0x520000, but addresses
  // are only valid starting at 0x520100.
  uint64_t page_alignment_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_ADDRESS_MAPPER_H_
