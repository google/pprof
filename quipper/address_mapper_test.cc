// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_mapper.h"

#include <algorithm>
#include <memory>

#include "base/logging.h"
#include "base/macros.h"

#include "compat/test.h"

namespace quipper {

namespace {

struct Range {
  uint64_t addr;
  uint64_t size;
  uint64_t id;
  uint64_t base_offset;

  Range() : addr(0), size(0), id(0), base_offset(0) {}

  Range(uint64_t addr, uint64_t size, uint64_t id, uint64_t base_offset)
      : addr(addr), size(size), id(id), base_offset(base_offset) {}

  bool contains(const uint64_t check_addr) const {
    return (check_addr >= addr && check_addr < addr + size);
  }
};

// Some address ranges to map.  It is important that none of these overlap with
// each other, nor are any two of them contiguous.
const Range kMapRanges[] = {
    Range(0xff000000, 0x100000, 0xdeadbeef, 0),
    Range(0x00a00000, 0x10000, 0xcafebabe, 0),
    Range(0x0c000000, 0x1000000, 0x900df00d, 0),
    Range(0x00001000, 0x30000, 0x9000091e, 0),
};

// List of real addresses that are not in the above ranges.
const uint64_t kAddressesNotInRanges[] = {
    0x00000000, 0x00000100, 0x00038000, 0x00088888, 0x00100000, 0x004fffff,
    0x00a20000, 0x00cc0000, 0x00ffffff, 0x03e00000, 0x0b000000, 0x0d100000,
    0x0fffffff, 0x1fffffff, 0x7ffffff0, 0xdffffff0, 0xfe000000, 0xffffffff,
};

// Number of regularly-spaced intervals within a mapped range to test.
const int kNumRangeTestIntervals = 8;

// A simple test function to convert a real address to a mapped address.
// Address ranges in |ranges| are mapped starting at address 0.
uint64_t GetMappedAddressFromRanges(const Range* ranges,
                                    const unsigned int num_ranges,
                                    const uint64_t addr) {
  unsigned int i;
  uint64_t mapped_range_addr;
  for (i = 0, mapped_range_addr = 0; i < num_ranges;
       mapped_range_addr += ranges[i].size, ++i) {
    const Range& range = ranges[i];
    if (range.contains(addr)) return (addr - range.addr) + mapped_range_addr;
  }
  return static_cast<uint64_t>(-1);
}

}  // namespace

// The unit test class for AddressMapper.
class AddressMapperTest : public ::testing::Test {
 public:
  AddressMapperTest() {}
  ~AddressMapperTest() {}

  virtual void SetUp() { mapper_.reset(new AddressMapper); }

 protected:
  // Maps a range using the AddressMapper and makes sure that it was successful.
  // Uses all fields of |range|, including id and base_offset.
  bool MapRange(const Range& range, bool remove_old_mappings) {
    VLOG(1) << "Mapping range at " << std::hex << range.addr
            << " with length of " << range.size << " and id " << range.id;
    return mapper_->MapWithID(range.addr, range.size, range.id,
                              range.base_offset, remove_old_mappings);
  }

  // Tests a range that has been mapped. |expected_mapped_addr| is the starting
  // address that it should have been mapped to. This mapper will test the start
  // and end addresses of the range, as well as a bunch of addresses inside it.
  // Also checks lookup of ID and offset.
  void TestMappedRange(const Range& range, uint64_t expected_mapped_addr) {
    uint64_t mapped_addr = UINT64_MAX;
    AddressMapper::MappingList::const_iterator addr_iter;

    VLOG(1) << "Testing range at " << std::hex << range.addr
            << " with length of " << std::hex << range.size;

    // Check address at the beginning of the range and at subsequent intervals.
    for (int i = 0; i < kNumRangeTestIntervals; ++i) {
      const uint64_t offset = i * (range.size / kNumRangeTestIntervals);
      uint64_t addr = range.addr + offset;
      EXPECT_TRUE(mapper_->GetMappedAddressAndListIterator(addr, &mapped_addr,
                                                           &addr_iter));
      EXPECT_EQ(expected_mapped_addr + offset, mapped_addr);

      uint64_t mapped_offset;
      uint64_t mapped_id;
      mapper_->GetMappedIDAndOffset(addr, addr_iter, &mapped_id,
                                    &mapped_offset);
      EXPECT_EQ(range.base_offset + offset, mapped_offset);
      EXPECT_EQ(range.id, mapped_id);
    }

    // Check address at end of the range.
    EXPECT_TRUE(mapper_->GetMappedAddressAndListIterator(
        range.addr + range.size - 1, &mapped_addr, &addr_iter));
    EXPECT_EQ(expected_mapped_addr + range.size - 1, mapped_addr);
  }

  std::unique_ptr<AddressMapper> mapper_;
};

// Map one range at a time and test looking up addresses.
TEST_F(AddressMapperTest, MapSingle) {
  for (const Range& range : kMapRanges) {
    mapper_.reset(new AddressMapper);
    ASSERT_TRUE(MapRange(range, false));
    EXPECT_EQ(1U, mapper_->GetNumMappedRanges());
    TestMappedRange(range, 0);

    // Check addresses before the mapped range, should be invalid.
    uint64_t mapped_addr;
    AddressMapper::MappingList::const_iterator iter;
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(range.addr - 1,
                                                          &mapped_addr, &iter));
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(range.addr - 0x100,
                                                          &mapped_addr, &iter));
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(
        range.addr + range.size, &mapped_addr, &iter));
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(
        range.addr + range.size + 0x100, &mapped_addr, &iter));
    EXPECT_EQ(range.size, mapper_->GetMaxMappedLength());
  }
}

// Map all the ranges at once and test looking up addresses.
TEST_F(AddressMapperTest, MapAll) {
  uint64_t size_mapped = 0;
  for (const Range& range : kMapRanges) {
    ASSERT_TRUE(MapRange(range, false));
    size_mapped += range.size;
  }
  EXPECT_EQ(arraysize(kMapRanges), mapper_->GetNumMappedRanges());

  // Check the max mapped length in quipper space.
  EXPECT_EQ(size_mapped, mapper_->GetMaxMappedLength());

  // For each mapped range, test addresses at the start, middle, and end.
  // Also test the address right before and after each range.
  uint64_t mapped_addr;
  AddressMapper::MappingList::const_iterator iter;
  for (const Range& range : kMapRanges) {
    TestMappedRange(range, GetMappedAddressFromRanges(
                               kMapRanges, arraysize(kMapRanges), range.addr));

    // Check addresses before and after the mapped range, should be invalid.
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(range.addr - 1,
                                                          &mapped_addr, &iter));
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(range.addr - 0x100,
                                                          &mapped_addr, &iter));
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(
        range.addr + range.size, &mapped_addr, &iter));
    EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(
        range.addr + range.size + 0x100, &mapped_addr, &iter));
  }

  // Test some addresses that are out of these ranges, should not be able to
  // get mapped addresses.
  for (uint64_t addr : kAddressesNotInRanges)
    EXPECT_FALSE(
        mapper_->GetMappedAddressAndListIterator(addr, &mapped_addr, &iter));
}

// Map all the ranges at once and test looking up IDs and offsets.
TEST_F(AddressMapperTest, MapAllWithIDsAndOffsets) {
  for (const Range& range : kMapRanges) {
    LOG(INFO) << "Mapping range at " << std::hex << range.addr
              << " with length of " << std::hex << range.size;
    ASSERT_TRUE(mapper_->MapWithID(range.addr, range.size, range.id, 0, false));
  }
  EXPECT_EQ(arraysize(kMapRanges), mapper_->GetNumMappedRanges());

  // For each mapped range, test addresses at the start, middle, and end.
  // Also test the address right before and after each range.
  for (const Range& range : kMapRanges) {
    TestMappedRange(range, GetMappedAddressFromRanges(
                               kMapRanges, arraysize(kMapRanges), range.addr));
  }
}

// Test overlap detection.
TEST_F(AddressMapperTest, OverlapSimple) {
  // Map all the ranges first.
  for (const Range& range : kMapRanges) ASSERT_TRUE(MapRange(range, false));

  // Attempt to re-map each range, but offset by size / 2.
  for (const Range& range : kMapRanges) {
    Range new_range;
    new_range.addr = range.addr + range.size / 2;
    new_range.size = range.size;
    // The maps should fail because of overlap with an existing mapping.
    EXPECT_FALSE(MapRange(new_range, false));
  }

  // Re-map each range with the same offset.  Only this time, remove any old
  // mapped range that overlaps with it.
  for (const Range& range : kMapRanges) {
    Range new_range;
    new_range.addr = range.addr + range.size / 2;
    new_range.size = range.size;
    EXPECT_TRUE(MapRange(new_range, true));
    // Make sure the number of ranges is unchanged (one deleted, one added).
    EXPECT_EQ(arraysize(kMapRanges), mapper_->GetNumMappedRanges());

    // The range is shifted in real space but should still be the same in
    // quipper space.
    TestMappedRange(
        new_range, GetMappedAddressFromRanges(kMapRanges, arraysize(kMapRanges),
                                              range.addr));
  }
}

// Test mapping of a giant map that overlaps with all existing ranges.
TEST_F(AddressMapperTest, OverlapBig) {
  // A huge region that overlaps with all ranges in |kMapRanges|.
  const Range kBigRegion(0xa00, 0xff000000, 0x1234, 0);

  // Map all the ranges first.
  for (const Range& range : kMapRanges) ASSERT_TRUE(MapRange(range, false));

  // Make sure overlap is detected before removing old ranges.
  ASSERT_FALSE(MapRange(kBigRegion, false));
  ASSERT_TRUE(MapRange(kBigRegion, true));
  EXPECT_EQ(1U, mapper_->GetNumMappedRanges());

  TestMappedRange(kBigRegion, 0);

  // Given the list of previously unmapped addresses, test that the ones within
  // |kBigRegion| are now mapped; for the ones that are not, test that they are
  // not mapped.
  for (uint64_t addr : kAddressesNotInRanges) {
    uint64_t mapped_addr = UINT64_MAX;
    AddressMapper::MappingList::const_iterator addr_iter;
    bool map_success = mapper_->GetMappedAddressAndListIterator(
        addr, &mapped_addr, &addr_iter);
    if (kBigRegion.contains(addr)) {
      EXPECT_TRUE(map_success);
      EXPECT_EQ(addr - kBigRegion.addr, mapped_addr);
    } else {
      EXPECT_FALSE(map_success);
    }
  }

  // Check that addresses in the originally mapped ranges no longer map to the
  // same addresses if they fall within |kBigRegion|, and don't map at all if
  // they are not within |kBigRegion|.
  for (const Range& range : kMapRanges) {
    for (uint64_t addr = range.addr; addr < range.addr + range.size;
         addr += range.size / kNumRangeTestIntervals) {
      uint64_t mapped_addr = UINT64_MAX;
      AddressMapper::MappingList::const_iterator addr_iter;
      bool map_success = mapper_->GetMappedAddressAndListIterator(
          addr, &mapped_addr, &addr_iter);
      if (kBigRegion.contains(addr)) {
        EXPECT_TRUE(map_success);
        EXPECT_EQ(addr - kBigRegion.addr, mapped_addr);
      } else {
        EXPECT_FALSE(map_success);
      }
    }
  }

  EXPECT_EQ(kBigRegion.size, mapper_->GetMaxMappedLength());
}

// Test a mapping at the end of memory space.
TEST_F(AddressMapperTest, EndOfMemory) {
  // A region that extends to the end of the address space.
  const Range kEndRegion(0xffffffff00000000, 0x100000000, 0x3456, 0);

  ASSERT_TRUE(MapRange(kEndRegion, true));
  EXPECT_EQ(1U, mapper_->GetNumMappedRanges());
  TestMappedRange(kEndRegion, 0);
}

// Test mapping of an out-of-bounds mapping.
TEST_F(AddressMapperTest, OutOfBounds) {
  // A region toward the end of address space that overruns the end of the
  // address space.
  const Range kOutOfBoundsRegion(0xffffffff00000000, 0x00000000, 0xccddeeff, 0);

  ASSERT_FALSE(MapRange(kOutOfBoundsRegion, false));
  ASSERT_FALSE(MapRange(kOutOfBoundsRegion, true));
  EXPECT_EQ(0, mapper_->GetNumMappedRanges());
  uint64_t mapped_addr;
  AddressMapper::MappingList::const_iterator iter;
  EXPECT_FALSE(mapper_->GetMappedAddressAndListIterator(
      kOutOfBoundsRegion.addr + 0x100, &mapped_addr, &iter));
}

// Test mapping of a region that covers the entire memory space.  Then map other
// regions over it.
TEST_F(AddressMapperTest, FullRange) {
  // A huge region that covers all of the available space.
  const Range kFullRegion(0, UINT64_MAX, 0xaabbccdd, 0);

  ASSERT_TRUE(MapRange(kFullRegion, false));
  size_t num_expected_ranges = 1;
  EXPECT_EQ(num_expected_ranges, mapper_->GetNumMappedRanges());

  TestMappedRange(kFullRegion, 0);

  // Map some smaller ranges.
  for (const Range& range : kMapRanges) {
    // Check for collision first.
    ASSERT_FALSE(MapRange(range, false));
    ASSERT_TRUE(MapRange(range, true));

    // Make sure the number of mapped ranges has increased by two.  The mapping
    // should have split an existing range.
    num_expected_ranges += 2;
    EXPECT_EQ(num_expected_ranges, mapper_->GetNumMappedRanges());
  }
}

// Test mapping of one range in the middle of an existing range. The existing
// range should be split into two to accommodate it. Also tests the use of base
// offsets.
TEST_F(AddressMapperTest, SplitRangeWithOffsetBase) {
  // Define the two ranges, with distinct IDs.
  const Range kFirstRange(0x10000, 0x4000, 0x11223344, 0x5000);
  const Range kSecondRange(0x12000, 0x1000, 0x55667788, 0);

  // As a sanity check, make sure the second range is fully contained within the
  // first range.
  EXPECT_LT(kFirstRange.addr, kSecondRange.addr);
  EXPECT_GT(kFirstRange.addr + kFirstRange.size,
            kSecondRange.addr + kSecondRange.size);

  // Map the two ranges.
  ASSERT_TRUE(MapRange(kFirstRange, true));
  ASSERT_TRUE(MapRange(kSecondRange, true));

  // The first range should have been split into two parts to make way for the
  // second range. There should be a total of three ranges.
  EXPECT_EQ(3U, mapper_->GetNumMappedRanges());

  // Now make sure the mappings are correct.

  // The first range has been split up. Define the expected ranges here.
  const Range kFirstRangeHead(0x10000, 0x2000, kFirstRange.id, 0x5000);
  const Range kFirstRangeTail(0x13000, 0x1000, kFirstRange.id, 0x8000);

  // Test the two remaining parts of the first range.
  TestMappedRange(kFirstRangeHead, 0);
  TestMappedRange(kFirstRangeTail, kFirstRangeTail.addr - kFirstRangeHead.addr);

  // Test the second range normally.
  TestMappedRange(kSecondRange, kSecondRange.addr - kFirstRange.addr);
}

// Test mappings that are not aligned to mmap page boundaries.
TEST_F(AddressMapperTest, NotPageAligned) {
  mapper_->set_page_alignment(0x1000);

  // Some address ranges that do not begin on a page boundary.
  const Range kUnalignedRanges[] = {
      Range(0xff000100, 0x1fff00, 0xdeadbeef, 0x100),
      Range(0x00a00180, 0x10000, 0xcafebabe, 0x180),
      Range(0x0c000300, 0x1000800, 0x900df00d, 0x4300),
      Range(0x000017f0, 0x30000, 0x9000091e, 0x7f0),
  };

  // Map the ranges.
  for (const Range& range : kUnalignedRanges)
    ASSERT_TRUE(MapRange(range, true));

  EXPECT_EQ(4U, mapper_->GetNumMappedRanges());

  // Now make sure the mappings are correct.

  // First region is mapped as usual, except it does not start at the page
  // boundary.
  TestMappedRange(kUnalignedRanges[0], 0x00000100);

  // Second region follows at the end of the first, but notice that its length
  // is a full number of pages, which means...
  TestMappedRange(kUnalignedRanges[1], 0x00200180);

  // ... the third region starts beyond the next page boundary: 0x211000 instead
  // of 0x210000.
  TestMappedRange(kUnalignedRanges[2], 0x00211300);

  // Similarly, the fourth region starts beyond the next page boundary:
  // 0x1212000 instead of 0x1211000.
  TestMappedRange(kUnalignedRanges[3], 0x012127f0);
}

// Have one mapping in the middle of another, with a nonzero page alignment
// parameter.
TEST_F(AddressMapperTest, SplitRangeWithPageAlignment) {
  mapper_->set_page_alignment(0x1000);

  // These should map just fine.
  const Range kRange0(0x3000, 0x8000, 0xdeadbeef, 0);
  const Range kRange1(0x5000, 0x2000, 0xfeedbabe, 0);

  EXPECT_TRUE(MapRange(kRange0, true));
  EXPECT_TRUE(MapRange(kRange1, true));

  EXPECT_EQ(3U, mapper_->GetNumMappedRanges());

  // Determine the expected split mappings.
  const Range kRange0Head(0x3000, 0x2000, 0xdeadbeef, 0);
  const Range kRange0Tail(0x7000, 0x4000, 0xdeadbeef, 0x4000);

  // Everything should be mapped and split as usual.
  TestMappedRange(kRange0Head, 0);
  TestMappedRange(kRange0Tail, 0x4000);
  TestMappedRange(kRange1, 0x2000);
}

// Have one mapping in the middle of another, with a nonzero page alignment
// parameter. The overlapping region will not be aligned to page boundaries.
TEST_F(AddressMapperTest, MisalignedSplitRangeWithPageAlignment) {
  mapper_->set_page_alignment(0x1000);

  const Range kRange0(0x3000, 0x8000, 0xdeadbeef, 0);
  const Range kMisalignedRange(0x4800, 0x2000, 0xfeedbabe, 0);

  EXPECT_TRUE(MapRange(kRange0, true));
  // The misaligned mapping should not find enough space to split the existing
  // range. It is not allowed to move the existing mapping.
  EXPECT_FALSE(MapRange(kMisalignedRange, true));
}

}  // namespace quipper
