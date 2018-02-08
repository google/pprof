// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_MYBASE_BASE_MACROS_H_
#define CHROMIUMOS_WIDE_PROFILING_MYBASE_BASE_MACROS_H_

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

#define arraysize(x) (sizeof(x) / sizeof(*x))

#endif  // CHROMIUMOS_WIDE_PROFILING_MYBASE_BASE_MACROS_H_
