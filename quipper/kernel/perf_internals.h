// Copied from kernel sources. See COPYING for license details.

#ifndef PERF_INTERNALS_H_
#define PERF_INTERNALS_H_

#include <linux/limits.h>
#include <stddef.h>  // For NULL
#include <stdint.h>
#include <sys/types.h>  // For pid_t

#include "perf_event.h"  

namespace quipper {

// These typedefs are from tools/perf/util/types.h in the kernel.
typedef uint64_t u64;
typedef int64_t s64;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned short u16;  
typedef signed short s16;    
typedef unsigned char u8;
typedef signed char s8;

// The first 64 bits of the perf header, used as a perf data file ID tag.
const uint64_t kPerfMagic = 0x32454c4946524550LL;  // "PERFILE2" little-endian

// These data structures have been copied from the kernel. See files under
// tools/perf/util.

//
// From tools/perf/util/header.h
//

enum {
  HEADER_RESERVED = 0, /* always cleared */
  HEADER_FIRST_FEATURE = 1,
  HEADER_TRACING_DATA = 1,
  HEADER_BUILD_ID,

  HEADER_HOSTNAME,
  HEADER_OSRELEASE,
  HEADER_VERSION,
  HEADER_ARCH,
  HEADER_NRCPUS,
  HEADER_CPUDESC,
  HEADER_CPUID,
  HEADER_TOTAL_MEM,
  HEADER_CMDLINE,
  HEADER_EVENT_DESC,
  HEADER_CPU_TOPOLOGY,
  HEADER_NUMA_TOPOLOGY,
  HEADER_BRANCH_STACK,
  HEADER_PMU_MAPPINGS,
  HEADER_GROUP_DESC,
  HEADER_LAST_FEATURE,
  HEADER_FEAT_BITS = 256,
};

struct perf_file_section {
  u64 offset;
  u64 size;
};

struct perf_file_attr {
  struct perf_event_attr attr;
  struct perf_file_section ids;
};

#define BITS_PER_BYTE 8
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define BITS_TO_LONGS(nr) \
  DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))  

#define DECLARE_BITMAP(name, bits) \
  unsigned long name[BITS_TO_LONGS(bits)]  

struct perf_file_header {
  u64 magic;
  u64 size;
  u64 attr_size;
  struct perf_file_section attrs;
  struct perf_file_section data;
  struct perf_file_section event_types;
  DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
};

#undef BITS_PER_BYTE
#undef DIV_ROUND_UP
#undef BITS_TO_LONGS
#undef DECLARE_BITMAP

struct perf_pipe_file_header {
  u64 magic;
  u64 size;
};

//
// From tools/perf/util/event.h
//

struct mmap_event {
  struct perf_event_header header;
  u32 pid, tid;
  u64 start;
  u64 len;
  u64 pgoff;
  char filename[PATH_MAX];
};

struct mmap2_event {
  struct perf_event_header header;
  u32 pid, tid;
  u64 start;
  u64 len;
  u64 pgoff;
  u32 maj;
  u32 min;
  u64 ino;
  u64 ino_generation;
  u32 prot;
  u32 flags;
  char filename[PATH_MAX];
};

struct comm_event {
  struct perf_event_header header;
  u32 pid, tid;
  char comm[16];
};

struct fork_event {
  struct perf_event_header header;
  u32 pid, ppid;
  u32 tid, ptid;
  u64 time;
};

struct lost_event {
  struct perf_event_header header;
  u64 id;
  u64 lost;
};

/*
 * PERF_FORMAT_ENABLED | PERF_FORMAT_RUNNING | PERF_FORMAT_ID
 */
struct read_event {
  struct perf_event_header header;
  u32 pid, tid;
  u64 value;
  u64 time_enabled;
  u64 time_running;
  u64 id;
};

struct throttle_event {
  struct perf_event_header header;
  u64 time;
  u64 id;
  u64 stream_id;
};

#define PERF_SAMPLE_MASK                                                    \
  (PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR | \
   PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_CPU |               \
   PERF_SAMPLE_PERIOD | PERF_SAMPLE_IDENTIFIER)

/* perf sample has 16 bits size limit */
#define PERF_SAMPLE_MAX_SIZE (1 << 16)

struct sample_event {
  struct perf_event_header header;
  u64 array[];
};

#if 0
// PERF_REGS_MAX is arch-dependent, so this is not a useful struct as-is.
struct regs_dump {
  u64 abi;
  u64 mask;
  u64 *regs;

  /* Cached values/mask filled by first register access. */
  u64 cache_regs[PERF_REGS_MAX];
  u64 cache_mask;
};
#endif

struct stack_dump {
  u16 offset;
  u64 size;
  char *data;
};

struct sample_read_value {
  u64 value;
  u64 id;
};

struct sample_read {
  u64 time_enabled;
  u64 time_running;
  struct {
    struct {
      u64 nr;
      struct sample_read_value *values;
    } group;
    struct sample_read_value one;
  };
};

struct ip_callchain {
  u64 nr;
  u64 ips[0];
};

struct branch_flags {
  u64 mispred : 1;
  u64 predicted : 1;
  u64 in_tx : 1;
  u64 abort : 1;
  u64 reserved : 60;
};

struct branch_entry {
  u64 from;
  u64 to;
  struct branch_flags flags;
};

struct branch_stack {
  u64 nr;
  struct branch_entry entries[0];
};

// All the possible fields of a perf sample.  This is not an actual data
// structure found in raw perf data, as each field may or may not be present in
// the data.
struct perf_sample {
  u64 ip;
  u32 pid, tid;
  u64 time;
  u64 addr;
  u64 id;
  u64 stream_id;
  u64 period;
  u64 weight;
  u64 transaction;
  u32 cpu;
  u32 raw_size;
  u64 data_src;
  u32 flags;
  u16 insn_len;
  void *raw_data;
  struct ip_callchain *callchain;
  struct branch_stack *branch_stack;
  // struct regs_dump  user_regs;  // See struct regs_dump above.
  struct stack_dump user_stack;
  struct sample_read read;

  perf_sample() : raw_data(NULL), callchain(NULL), branch_stack(NULL) {
    read.group.values = NULL;
  }
  ~perf_sample() {
    delete[] callchain;
    delete[] branch_stack;
    delete[] reinterpret_cast<char *>(raw_data);
    delete[] read.group.values;
  }
};

// Taken from tools/perf/util/include/linux/kernel.h
#define ALIGN(x, a) __ALIGN_MASK(x, (decltype(x))(a)-1)
#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))

// If this is changed, kBuildIDArraySize in perf_reader.h must also be changed.
#define BUILD_ID_SIZE 20

struct build_id_event {
  struct perf_event_header header;
  pid_t pid;
  u8 build_id[ALIGN(BUILD_ID_SIZE, sizeof(u64))];
  char filename[];
};

#undef ALIGN
#undef __ALIGN_MASK
#undef BUILD_ID_SIZE

enum perf_user_event_type {
  /* above any possible kernel type */
  PERF_RECORD_USER_TYPE_START = 64,
  PERF_RECORD_HEADER_ATTR = 64,
  PERF_RECORD_HEADER_EVENT_TYPE = 65, /* depreceated */
  PERF_RECORD_HEADER_TRACING_DATA = 66,
  PERF_RECORD_HEADER_BUILD_ID = 67,
  PERF_RECORD_FINISHED_ROUND = 68,
  PERF_RECORD_ID_INDEX = 69,
  PERF_RECORD_AUXTRACE_INFO = 70,
  PERF_RECORD_AUXTRACE = 71,
  PERF_RECORD_AUXTRACE_ERROR = 72,
  PERF_RECORD_THREAD_MAP = 73,
  PERF_RECORD_CPU_MAP = 74,
  PERF_RECORD_STAT_CONFIG = 75,
  PERF_RECORD_STAT = 76,
  PERF_RECORD_STAT_ROUND = 77,
  PERF_RECORD_EVENT_UPDATE = 78,
  PERF_RECORD_TIME_CONV = 79,
  PERF_RECORD_HEADER_FEATURE = 80,
  PERF_RECORD_HEADER_MAX = 81,
};

struct attr_event {
  struct perf_event_header header;
  struct perf_event_attr attr;
  u64 id[];
};

#define MAX_EVENT_NAME 64

struct perf_trace_event_type {
  u64 event_id;
  char name[MAX_EVENT_NAME];
};

#undef MAX_EVENT_NAME

struct event_type_event {
  struct perf_event_header header;
  struct perf_trace_event_type event_type;
};

struct tracing_data_event {
  struct perf_event_header header;
  u32 size;
};

struct auxtrace_event {
  struct perf_event_header header;
  u64 size;
  u64 offset;
  u64 reference;
  u32 idx;
  u32 tid;
  u32 cpu;
  u32 reserved__; /* For alignment */
};

struct aux_event {
  struct perf_event_header header;
  u64 aux_offset;
  u64 aux_size;
  u64 flags;
};

union perf_event {
  struct perf_event_header header;
  struct mmap_event mmap;
  struct mmap2_event mmap2;
  struct comm_event comm;
  struct fork_event fork;
  struct lost_event lost;
  struct read_event read;
  struct throttle_event throttle;
  struct sample_event sample;
  struct attr_event attr;
  struct event_type_event event_type;
  struct tracing_data_event tracing_data;
  struct build_id_event build_id;
  struct auxtrace_event auxtrace;
  struct aux_event aux;
};

typedef perf_event event_t;

}  // namespace quipper

#endif /*PERF_INTERNALS_H_*/
