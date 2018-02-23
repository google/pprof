// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_test_files.h"

namespace perf_test_files {

const std::vector<const char*>& GetPerfDataFiles() {
  static const std::vector<const char*>* files = new std::vector<const char*>{
      // The following perf data contains the following event types, as passed
      // to perf record via the -e option:
      // - cycles
      // - instructions
      // - cache-references
      // - cache-misses
      // - branches
      // - branch-misses

      // Obtained with "perf record -- echo > /dev/null"
      "perf.data.singleprocess-3.4",

      // Obtained with "perf record -a -- sleep $N", for N in {0, 1, 5}.
      "perf.data.systemwide.0-3.4",
#ifdef TEST_LARGE_PERF_DATA
      "perf.data.systemwide.1-3.4",
      "perf.data.systemwide.5-3.4",

      // Obtained with "perf record -a -- sleep $N", for N in {0, 1, 5}.
      // While in the background, this loop is running:
      //   while true; do ls > /dev/null; done
      "perf.data.busy.0-3.4",
      "perf.data.busy.1-3.4",
      "perf.data.busy.5-3.4",
#endif  // defined(TEST_LARGE_PERF_DATA)

      // Obtained with "perf record -a -- sleep 2"
      // While in the background, this loop is running:
      //   while true; do restart powerd; sleep .2; done
      "perf.data.forkexit-3.4",

#ifdef TEST_CALLGRAPH
      // Obtained with "perf record -a -g -- sleep 2"
      "perf.data.callgraph-3.4",
#endif
      // Obtained with "perf record -a -b -- sleep 2"
      "perf.data.branch-3.4",
#ifdef TEST_CALLGRAPH
      // Obtained with "perf record -a -g -b -- sleep 2"
      "perf.data.callgraph_and_branch-3.4",
#endif

      // Obtained with "perf record -a -R -- sleep 2"
      "perf.data.raw-3.4",
#ifdef TEST_CALLGRAPH
      // Obtained with "perf record -a -R -g -b -- sleep 2"
      "perf.data.raw_callgraph_branch-3.4",
#endif

      // Data from other architectures.
      "perf.data.i686-3.4",   // 32-bit x86
      "perf.data.armv7-3.4",  // ARM v7

      // Same as above, obtained from a system running kernel v3.8.
      "perf.data.singleprocess-3.8",
      "perf.data.systemwide.0-3.8",
#ifdef TEST_LARGE_PERF_DATA
      "perf.data.systemwide.1-3.8",
      "perf.data.systemwide.5-3.8",
      "perf.data.busy.0-3.8",
      "perf.data.busy.1-3.8",
      "perf.data.busy.5-3.8",
#endif  // defined(TEST_LARGE_PERF_DATA)

      "perf.data.forkexit-3.8",
#ifdef TEST_CALLGRAPH
      "perf.data.callgraph-3.8",
#endif
      "perf.data.branch-3.8",
#ifdef TEST_CALLGRAPH
      "perf.data.callgraph_and_branch-3.8",
#endif
      "perf.data.armv7.perf_3.14-3.8",  // ARM v7 obtained using perf 3.14.

      // Obtained from a system that uses NUMA topology.
      "perf.data.numatopology-3.2",

      // Obtained to test GROUP_DESC feature
      "perf.data.group_desc-4.4",

      // Perf data that contains hardware and software events.
      // Command:
      //    perf record -a -c 1000000 -e cycles,branch-misses,cpu-clock -- \
      //    sleep 2
      // HW events are cycles and branch-misses, SW event is cpu-clock.
      // This also tests non-consecutive event types.
      "perf.data.hw_and_sw-3.4",

      // This test first mmap()s a DSO, then fork()s to copy the mapping to the
      // child and then modifies the mapping by mmap()ing a DSO on top of the
      // old one. It then records SAMPLEs events in the child. It ensures the
      // SAMPLEs in the child are attributed to the first DSO that was mmap()ed,
      // not the second one.
      "perf.data.remmap-3.2",

      // This is sample with a frequency higher than the max frequency, so it
      // has throttle and unthrottle events.
      "perf.data.throttle-3.8",

      // Perf data that contains intel pt events from perf-4.14
      // Command:
      //    perf record -e intel_pt// -e cycles -o /tmp/perf.data.intel_pt-4.14
      //    -- echo "Hello, World!"
      "perf.data.intel_pt-4.14",
  };
  return *files;
}

const std::vector<const char*>& GetPerfPipedDataFiles() {
  static const std::vector<const char*>* files = new std::vector<const char*>{
      "perf.data.piped.target-3.4",
      "perf.data.piped.target.throttled-3.4",
      "perf.data.piped.target-3.8",

      /* Piped data that contains hardware and software events.
       * Command:
       *    perf record -a -c 1000000 -e cycles,branch-misses,cpu-clock -o - \
       *    -- sleep 2
       * HW events are cycles and branch-misses, SW event is cpu-clock.
       */
      "perf.data.piped.hw_and_sw-3.4",

      // Piped data with extra data at end.
      "perf.data.piped.extrabyte-3.4",
      "perf.data.piped.extradata-3.4",

      // Perf data that contains intel pt events collected in piped mode from
      // perf-4.14
      // Command:
      //    perf record -e intel_pt// -e cycles -o - -- echo "Hello, World!" | \
      //    cat &> /tmp/perf.data.piped.intel_pt-4.14
      "perf.data.piped.intel_pt-4.14",
  };
  return *files;
}

const std::vector<const char*>& GetCorruptedPerfPipedDataFiles() {
  static const std::vector<const char*>* files = new std::vector<const char*>{
      // Has a SAMPLE event with size set to zero. Don't go into an infinite
      // loop.
      "perf.data.piped.corrupted.zero_size_sample-3.2",
  };
  return *files;
}

const std::vector<const char*>& GetPerfDataProtoFiles() {
  static const std::vector<const char*>* files = new std::vector<const char*>{
      "perf.callgraph.pb_text",
  };
  return *files;
}

}  // namespace perf_test_files
