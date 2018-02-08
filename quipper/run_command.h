// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_RUN_COMMAND_H_
#define CHROMIUMOS_WIDE_PROFILING_RUN_COMMAND_H_

#include <string>
#include <vector>

#include "compat/string.h"

namespace quipper {

// Executes |command|. stderr is directed to /dev/null. If |output| is not null,
// stdout is stored in |output|, and to /dev/null otherwise. Returns the exit
// status of the command if it exited normally, or -1 otherwise. If the call
// to exec failed, then errno is set accordingly.
int RunCommand(const std::vector<string>& command, std::vector<char>* output);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_RUN_COMMAND_H_
