#include "huge_page_deducer.h"  

#include "compat/test.h"

using PerfEvent = quipper::PerfDataProto::PerfEvent;
using MMapEvent = quipper::PerfDataProto::MMapEvent;

namespace quipper {
namespace {

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

  ASSERT_EQ(3, events.size());

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
