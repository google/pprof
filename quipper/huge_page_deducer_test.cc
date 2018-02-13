#include "huge_page_deducer.h"  

#include "base/logging.h"
#include "compat/string.h"
#include "compat/test.h"


namespace quipper {
namespace {
using PerfEvent = PerfDataProto::PerfEvent;
using MMapEvent = PerfDataProto::MMapEvent;
using ::testing::EqualsProto;
using ::testing::Pointwise;
using ::testing::proto::Partially;

// AddMmap is a helper function to create simple MMapEvents, with which
// testcases can encode "maps" entries similar to /proc/self/maps in a tabular
// one-line-per-entry.
void AddMmap(uint32_t pid, uint64_t mmap_start, uint64_t length, uint64_t pgoff,
             const string& file, RepeatedPtrField<PerfEvent>* events) {
  MMapEvent* ev = events->Add()->mutable_mmap_event();
  ev->set_pid(pid);
  ev->set_start(mmap_start);
  ev->set_len(length);
  ev->set_pgoff(pgoff);
  ev->set_filename(file);
}

TEST(HugePageDeducer, HugePagesMappings) {
  RepeatedPtrField<PerfEvent> events;
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x40000000);
    ev->set_len(0x18000);
    ev->set_pgoff(0);
    ev->set_filename("/usr/lib/libfoo.so");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x40018000);
    ev->set_len(0x1e8000);
    ev->set_pgoff(0);
    ev->set_filename("/opt/google/chrome/chrome");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x40200000);
    ev->set_len(0x1c00000);
    ev->set_pgoff(0);
    ev->set_filename("//anon");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x41e00000);
    ev->set_len(0x4000000);
    ev->set_pgoff(0x1de8000);
    ev->set_filename("/opt/google/chrome/chrome");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(2345);
    ev->set_start(0x45e00000);
    ev->set_len(0x1e00000);
    ev->set_pgoff(0);
    ev->set_filename("//anon");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(2345);
    ev->set_start(0x47c00000);
    ev->set_len(0x4000000);
    ev->set_pgoff(0x1e00000);
    ev->set_filename("/opt/google/chrome/chrome");
  }

  DeduceHugePages(&events);
  CombineMappings(&events);

  ASSERT_GE(events.size(), 3);
  EXPECT_EQ(events.size(), 3);

  EXPECT_THAT(events,
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { start: 0x40000000 len:0x18000 "
                            "pgoff: 0 filename: '/usr/lib/libfoo.so'}",
                            "mmap_event: { start: 0x40018000 len:0x5de8000 "
                            "pgoff: 0 filename: '/opt/google/chrome/chrome'}",
                            "mmap_event: { start: 0x45e00000 len:0x5e00000 "
                            "pgoff: 0 filename: '/opt/google/chrome/chrome'}",
                        }));

  EXPECT_EQ("/usr/lib/libfoo.so", events[0].mmap_event().filename());
  EXPECT_EQ(0x40000000, events[0].mmap_event().start());
  EXPECT_EQ(0x18000, events[0].mmap_event().len());
  EXPECT_EQ(0x0, events[0].mmap_event().pgoff());

  // The split Chrome mappings should have been combined.
  EXPECT_EQ("/opt/google/chrome/chrome", events[2].mmap_event().filename());
  EXPECT_EQ(0x40018000, events[1].mmap_event().start());
  EXPECT_EQ(0x5de8000, events[1].mmap_event().len());
  EXPECT_EQ(0x0, events[1].mmap_event().pgoff());

  EXPECT_EQ("/opt/google/chrome/chrome", events[2].mmap_event().filename());
  EXPECT_EQ(0x45e00000, events[2].mmap_event().start());
  EXPECT_EQ(0x5e00000, events[2].mmap_event().len());
  EXPECT_EQ(0x0, events[2].mmap_event().pgoff());
}

enum HugepageTextStyle {
  kAnonHugepageText,
  kNoHugepageText,
};

class HugepageTextStyleDependent
    : public ::testing::TestWithParam<HugepageTextStyle> {
 protected:
  void AddHugepageTextMmap(uint32_t pid, uint64_t mmap_start, uint64_t length,
                           uint64_t pgoff, string file,
                           RepeatedPtrField<PerfEvent>* events) {
    // Various hugepage implementations and perf versions result in various
    // quirks in how hugepages are reported.

    switch (GetParam()) {
      case kNoHugepageText:
        // Do nothing; the maps are complete and file-backed
        break;
      case kAnonHugepageText:
        // exec is remapped into anonymous memory, which perf reports as
        // '//anon'. Anonymous sections have no pgoff.
        file = "//anon";
        pgoff = 0;
        break;
      default:
        CHECK(false) << "Unimplemented";
    }
    AddMmap(pid, mmap_start, length, pgoff, file, events);
  }
};

TEST_P(HugepageTextStyleDependent, OnlyOneMappingThatIsHuge) {
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(1, 0x100200000, 0x200000, 0, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  // Don't check filename='file'; if it's backed by anonymous memory, it isn't
  // possible for quipper to deduce the filename without other mmaps immediately
  // adjacent.
  EXPECT_THAT(
      events,
      Pointwise(Partially(EqualsProto()),
                {"mmap_event: { start: 0x100200000 len: 0x200000 pgoff: 0}"}));
}

TEST_P(HugepageTextStyleDependent, OnlyOneMappingUnaligned) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(2, 0x200201000, 0x200000, 0, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x200201000 "
                                 "len:0x200000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, FirstPageIsHugeWithSmallTail) {
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(3, 0x300400000, 0x400000, 0, "file", &events);
  AddMmap(3, 0x300800000, 0x001000, 0x400000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x300400000 "
                                 "len:0x401000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, DISABLED_FirstPageIsSmallWithHugeTail) {
  // This test is disabled because DeduceHugePage requires a non-zero pgoff
  // *after* a hugepage_text section in order to correctly deduce it, so it
  // is unable to deduce these cases.
  RepeatedPtrField<PerfEvent> events;
  AddMmap(4, 0x4003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(4, 0x400400000, 0x200000, 0x001000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x4003ff000 "
                                 "len:0x201000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageBetweenTwoSmallSections) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(5, 0x5003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(5, 0x500400000, 0x200000, 0x001000, "file", &events);
  AddMmap(5, 0x500600000, 0x001000, 0x201000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x5003ff000 "
                                 "len:0x202000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageSplitByEarlyMlockBetweenTwoSmall) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(6, 0x6003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(6, 0x600400000, 0x3f8000, 0x001000, "file", &events);
  AddHugepageTextMmap(6, 0x6007f8000, 0x008000, 0x3f9000, "file", &events);
  AddMmap(6, 0x600800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x6003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageSplitByLateMlockBetweenTwoSmall) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(7, 0x7003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(7, 0x700400000, 0x008000, 0x001000, "file", &events);
  AddHugepageTextMmap(7, 0x700408000, 0x3f8000, 0x009000, "file", &events);
  AddMmap(7, 0x700800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x7003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageSplitEvenlyByMlockBetweenTwoSmall) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(8, 0x8003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(8, 0x800400000, 0x0f8000, 0x001000, "file", &events);
  AddHugepageTextMmap(8, 0x8004f8000, 0x008000, 0x0f9000, "file", &events);
  AddHugepageTextMmap(8, 0x800500000, 0x100000, 0x101000, "file", &events);
  AddMmap(8, 0x800600000, 0x001000, 0x201000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x8003ff000 "
                                 "len:0x202000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, MultipleContiguousHugepages) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(9, 0x9003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(9, 0x900400000, 0x200000, 0x001000, "file", &events);
  AddHugepageTextMmap(9, 0x900600000, 0x200000, 0x201000, "file", &events);
  AddMmap(9, 0x900800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x9003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, MultipleContiguousMlockSplitHugepages) {
  // Think:
  // - hugepage_text 4MiB range
  // - mlock alternating 512-KiB chunks
  RepeatedPtrField<PerfEvent> events;
  AddMmap(10, 0xa003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(10, 0xa00400000, 0x080000, 0x001000, "file", &events);
  AddHugepageTextMmap(10, 0xa00480000, 0x080000, 0x081000, "file", &events);
  AddHugepageTextMmap(10, 0xa00500000, 0x080000, 0x101000, "file", &events);
  AddHugepageTextMmap(10, 0xa00580000, 0x080000, 0x181000, "file", &events);
  AddHugepageTextMmap(10, 0xa00600000, 0x080000, 0x201000, "file", &events);
  AddHugepageTextMmap(10, 0xa00680000, 0x080000, 0x281000, "file", &events);
  AddHugepageTextMmap(10, 0xa00700000, 0x080000, 0x301000, "file", &events);
  AddHugepageTextMmap(10, 0xa00780000, 0x080000, 0x381000, "file", &events);
  AddMmap(10, 0xa00800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0xa003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, MultipleWithUnalignedInitialHugePage) {
  // Base on real program
  RepeatedPtrField<PerfEvent> events;

  AddHugepageTextMmap(11, 0x85d32e000, 0x6d2000, 0x0, "file", &events);
  AddHugepageTextMmap(11, 0x85da00000, 0x6a00000, 0x6d2000, "file", &events);
  AddMmap(11, 0x864400000, 0x200000, 0x70d2000, "file", &events);
  AddHugepageTextMmap(11, 0x864600000, 0x200000, 0x72d2000, "file", &events);
  AddMmap(11, 0x864800000, 0x600000, 0x74d2000, "file", &events);
  AddHugepageTextMmap(11, 0x864e00000, 0x200000, 0x7ad2000, "file", &events);
  AddMmap(11, 0x865000000, 0x4a000, 0x7cd2000, "file", &events);
  AddMmap(11, 0x86504a000, 0x1000, 0x7d1c000, "file", &events);
  AddMmap(11, 0xa3d368000, 0x3a96000, 0x0, "file2", &events);
  AddMmap(11, 0xa467cc000, 0x2000, 0x0, "file3", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {
                                    "mmap_event: { start: 0x85d32e000 "
                                    "len:0x7d1d000 pgoff: 0 filename: 'file'}",
                                    "mmap_event: { start: 0xa3d368000 "
                                    "len:0x3a96000 pgoff: 0 filename: 'file2'}",
                                    "mmap_event: { start: 0xa467cc000 "
                                    "len:0x2000,  pgoff: 0 filename: 'file3'}",
                                }));
}

TEST_P(HugepageTextStyleDependent, MultipleWithUnalignedInitialHugePage2) {
  // Base on real program
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(12, 0xbcff6000, 0x200000, 0x00000000, "file", &events);
  AddMmap(12, 0xbd1f6000, 0x300a000, 0x200000, "file", &events);
  AddHugepageTextMmap(12, 0xc0200000, 0x2b374000, 0x320a000, "file", &events);
  AddHugepageTextMmap(12, 0xeb574000, 0x514000, 0x2e57e000, "file", &events);
  AddHugepageTextMmap(12, 0xeba88000, 0x1d78000, 0x2ea92000, "file", &events);
  AddMmap(12, 0xed800000, 0x1200000, 0x3080a000, "file", &events);
  AddHugepageTextMmap(12, 0xeea00000, 0x200000, 0x31a0a000, "file", &events);
  AddMmap(12, 0xeec00000, 0x2800000, 0x31c0a000, "file", &events);
  AddHugepageTextMmap(12, 0xf1400000, 0x200000, 0x3440a000, "file", &events);
  AddMmap(12, 0xf1600000, 0x89f000, 0x3460a000, "file", &events);
  AddMmap(12, 0xf1e9f000, 0x1000, 0x34ea9000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0xbcff6000 "
                                 "len:0x34eaa000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, NoMmaps) {
  RepeatedPtrField<PerfEvent> events;
  events.Add();

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(EqualsProto(), std::vector<PerfEvent>(1)));
}
TEST_P(HugepageTextStyleDependent, MultipleNonMmaps) {
  RepeatedPtrField<PerfEvent> events;
  events.Add();
  events.Add();

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(EqualsProto(), std::vector<PerfEvent>(2)));
}
TEST_P(HugepageTextStyleDependent, NonMmapFirstMmap) {
  RepeatedPtrField<PerfEvent> events;
  events.Add();
  AddHugepageTextMmap(12, 0, 0x200000, 0, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"", "mmap_event: { pgoff: 0 }"}));
}
TEST_P(HugepageTextStyleDependent, NonMmapAfterLastMmap) {
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(12, 0, 0x200000, 0, "file", &events);
  events.Add();

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { pgoff: 0 }", ""}));
}

INSTANTIATE_TEST_CASE_P(NoHugepageText, HugepageTextStyleDependent,
                        ::testing::Values(kNoHugepageText));
INSTANTIATE_TEST_CASE_P(AnonHugepageText, HugepageTextStyleDependent,
                        ::testing::Values(kAnonHugepageText));

TEST(HugePageDeducer, DoesNotChangeVirtuallyContiguousPgoffNonContiguous) {
  // We've seen programs with strange memory layouts having virtually contiguous
  // memory backed by non-contiguous bits of a file.
  RepeatedPtrField<PerfEvent> events;
  AddMmap(758463, 0x2f278000, 0x20000, 0, "lib0.so", &events);
  AddMmap(758463, 0x2f29d000, 0x2000, 0, "shm", &events);
  AddMmap(758463, 0x2f2a2000, 0xa000, 0, "lib1.so", &events);
  AddMmap(758463, 0x3d400000, 0x9ee000, 0, "lib2.so", &events);
  AddMmap(758463, 0x3e000000, 0x16000, 0, "lib3.so", &events);
  AddMmap(758463, 0x3e400000, 0x270000, 0x1a00000, "shm", &events);
  AddMmap(758463, 0x3e670000, 0x10000, 0x1aaac000, "shm", &events);
  AddMmap(758463, 0x3e680000, 0x10000, 0x1b410000, "shm", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events,
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { pgoff: 0 filename: 'lib0.so' }",
                            "mmap_event: { pgoff: 0 filename: 'shm' }",
                            "mmap_event: { pgoff: 0 filename: 'lib1.so' }",
                            "mmap_event: { pgoff: 0 filename: 'lib2.so' }",
                            "mmap_event: { pgoff: 0 filename: 'lib3.so' }",
                            "mmap_event: { pgoff: 0x1a00000 filename: 'shm' }",
                            "mmap_event: { pgoff: 0x1aaac000 filename: 'shm' }",
                            "mmap_event: { pgoff: 0x1b410000 filename: 'shm' }",
                        }));
}

TEST(HugePageDeducer, IgnoresDynamicMmaps) {
  // Now, let's watch a binary hugepage_text itself.
  RepeatedPtrField<PerfEvent> events;
  AddMmap(6531, 0x560d76b25000, 0x24ce000, 0, "main", &events);
  events.rbegin()->set_timestamp(700413232676401);
  AddMmap(6531, 0x7f686a1ec000, 0x24000, 0, "ld.so", &events);
  events.rbegin()->set_timestamp(700413232691935);
  AddMmap(6531, 0x7ffea5dc8000, 0x2000, 0, "[vdso]", &events);
  events.rbegin()->set_timestamp(700413232701418);
  AddMmap(6531, 0x7f686a1e3000, 0x5000, 0, "lib1.so", &events);
  events.rbegin()->set_timestamp(700413232824216);
  AddMmap(6531, 0x7f686a1a8000, 0x3a000, 0, "lib2.so", &events);
  events.rbegin()->set_timestamp(700413232854520);
  AddMmap(6531, 0x7f6869ea7000, 0x5000, 0, "lib3.so", &events);
  events.rbegin()->set_timestamp(700413248827794);
  AddMmap(6531, 0x7f6867e00000, 0x200000, 0, "/anon_hugepage (deleted)",
          &events);
  events.rbegin()->set_timestamp(700413295816043);
  AddMmap(6531, 0x7f6867c00000, 0x200000, 0, "/anon_hugepage (deleted)",
          &events);
  events.rbegin()->set_timestamp(700413305947499);
  AddMmap(6531, 0x7f68663f8000, 0x1e00000, 0x7f68663f8000, "//anon", &events);
  events.rbegin()->set_timestamp(700413306012797);
  AddMmap(6531, 0x7f6866525000, 0x1a00000, 0x7f6866525000, "//anon", &events);
  events.rbegin()->set_timestamp(700413312132909);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(
      events,
      Pointwise(
          Partially(EqualsProto()),
          {
              "mmap_event: { pgoff: 0 filename: 'main' }",
              "mmap_event: { pgoff: 0 filename: 'ld.so' }",
              "mmap_event: { pgoff: 0 filename: '[vdso]' }",
              "mmap_event: { pgoff: 0 filename: 'lib1.so' }",
              "mmap_event: { pgoff: 0 filename: 'lib2.so' }",
              "mmap_event: { pgoff: 0 filename: 'lib3.so' }",
              "mmap_event: { pgoff: 0 filename: '/anon_hugepage (deleted)' }",
              "mmap_event: { pgoff: 0 filename: '/anon_hugepage (deleted)' }",
              "mmap_event: { pgoff: 0x7f68663f8000 filename: '//anon' }",
              "mmap_event: { pgoff: 0x7f6866525000 filename: '//anon' }",
          }));
}

TEST(HugePageDeducer, Regression62446346) {
  RepeatedPtrField<PerfEvent> events;

  // Perf infers the filename is "file", but at offset 0 for
  // hugepage-backed, anonymous mappings.
  //
  // vaddr start   - vaddr end     vaddr-size    elf-offset
  // [0x55a685bfb000-55a685dfb000) (0x200000)   @ 0]:          file
  // [0x55a685dfb000-55a687c00000) (0x1e05000)  @ 0x200000]:   file
  // [0x55a687c00000-55a6a5200000) (0x1d600000) @ 0]:          file
  // [0x55a6a5200000-55a6a6400000) (0x1200000)  @ 0x1f605000]: file
  // [0x55a6a6400000-55a6a6600000) (0x200000)   @ 0]:          file
  // [0x55a6a6600000-55a6a8800000) (0x2200000)  @ 0x20a05000]: file
  // [0x55a6a8800000-55a6a8a00000) (0x200000)   @ 0]:          file
  // [0x55a6a8a00000-55a6a90ca000) (0x6ca000)   @ 0x22e05000]: file
  // [0x55a6a90ca000-55a6a90cb000) (0x1000)     @ 0x234cf000]: file
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a685bfb000);
    ev->set_len(0x200000);
    ev->set_pgoff(0);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a685dfb000);
    ev->set_len(0x1e05000);
    ev->set_pgoff(0x200000);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a687c00000);
    ev->set_len(0x1d600000);
    ev->set_pgoff(0);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a6a5200000);
    ev->set_len(0x1200000);
    ev->set_pgoff(0x1f605000);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a6a6400000);
    ev->set_len(0x200000);
    ev->set_pgoff(0);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a6a6600000);
    ev->set_len(0x2200000);
    ev->set_pgoff(0x20a05000);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a6a8800000);
    ev->set_len(0x200000);
    ev->set_pgoff(0);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a6a8a00000);
    ev->set_len(0x6ca000);
    ev->set_pgoff(0x22e05000);
    ev->set_filename("file");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x55a6a90ca000);
    ev->set_len(0x1000);
    ev->set_pgoff(0x234cf000);
    ev->set_filename("file");
  }

  DeduceHugePages(&events);
  CombineMappings(&events);

  ASSERT_EQ(1, events.size());

  EXPECT_EQ("file", events[0].mmap_event().filename());
  EXPECT_EQ(0x55a685bfb000, events[0].mmap_event().start());
  EXPECT_EQ(0x55a6a90cb000 - 0x55a685bfb000, events[0].mmap_event().len());
  EXPECT_EQ(0, events[0].mmap_event().pgoff());
}

}  // namespace
}  // namespace quipper
