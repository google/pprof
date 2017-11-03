// Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_level.h"

#include "base/commandlineflags.h"
#include "base/logging.h"
#include "base/vlog_is_on.h"


namespace quipper {

void SetVerbosityLevel(int level) {
  if (level > 0)
    SetVLOGLevel("*", level);
  else
    FLAGS_minloglevel = -level;
}

}  // namespace quipper

