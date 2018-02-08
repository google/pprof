// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

namespace logging {

namespace {

// The minimum logging level. Anything higher than this will be logged. Set to
// negative values to enable verbose logging.
int g_min_log_level = INFO;

}  // namespace

void SetMinLogLevel(int level) { g_min_log_level = level; }

int GetMinLogLevel() { return g_min_log_level; }

int GetVlogVerbosity() { return -g_min_log_level; }

}  // namespace logging
