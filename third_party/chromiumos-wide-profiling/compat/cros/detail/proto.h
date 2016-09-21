// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_CROS_DETAIL_PROTO_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_CROS_DETAIL_PROTO_H_

#include <google/protobuf/repeated_field.h>
#include <google/protobuf/text_format.h>

#include "perf_data.pb.h"  // NOLINT(build/include)
#include "perf_stat.pb.h"  // NOLINT(build/include)

namespace quipper {

using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::TextFormat;
using ::google::protobuf::uint64;

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_CROS_DETAIL_PROTO_H_
