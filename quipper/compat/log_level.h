// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_LOG_LEVEL_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_LOG_LEVEL_H_

namespace quipper {

// Specify a verbosity level for logging functions. Higher values mean greater
// verbosity. VLOG() with level equal to or greater than this one will be
// printed. To filter LOG(INFO), LOG(WARNING), and LOG(ERROR), use negative
// values.
void SetVerbosityLevel(int level);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_LOG_LEVEL_H_
