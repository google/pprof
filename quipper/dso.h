// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_DSO_H_
#define CHROMIUMOS_WIDE_PROFILING_DSO_H_

#include <sys/stat.h>

#include <unordered_set>
#include <utility>

#include "compat/string.h"
#include "data_reader.h"

namespace quipper {

// Defines a type for a pid:tid pair.
using PidTid = std::pair<u32, u32>;

// A struct containing all relevant info for a mapped DSO, independent of any
// samples.
struct DSOInfo {
  string name;
  string build_id;
  u32 maj = 0;
  u32 min = 0;
  u64 ino = 0;
  bool hit = false;  // Have we seen any samples in this DSO?
  // unordered_set of pids this DSO had samples in.
  std::unordered_set<uint64_t> threads;
};

// Do the |DSOInfo| and |struct stat| refer to the same inode?
bool SameInode(const DSOInfo& dso, const struct stat* s);

// Must be called at least once before using libelf.
void InitializeLibelf();
// Read buildid from an ELF file using libelf.
bool ReadElfBuildId(const string& filename, string* buildid);
bool ReadElfBuildId(int fd, string* buildid);

// Read buildid from /sys/module/<module_name>/notes/.note.gnu.build-id
// (Does not use libelf.)
bool ReadModuleBuildId(const string& module_name, string* buildid);
// Read builid from Elf note data section.
bool ReadBuildIdNote(DataReader* data, string* buildid);

// Is |name| match one of the things reported by the kernel that is known
// not to be a kernel module?
bool IsKernelNonModuleName(string name);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_DSO_H_
