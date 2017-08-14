// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_data_utils.h"

#include "compat/string.h"
#include "compat/test.h"

namespace quipper {

TEST(PerfDataUtilsTest, GetUint64AlignedStringLength) {
  EXPECT_EQ(8, GetUint64AlignedStringLength("012345"));
  EXPECT_EQ(8, GetUint64AlignedStringLength("0123456"));
  EXPECT_EQ(16, GetUint64AlignedStringLength("01234567"));  // Room for '\0'
  EXPECT_EQ(16, GetUint64AlignedStringLength("012345678"));
  EXPECT_EQ(16, GetUint64AlignedStringLength("0123456789abcde"));
  EXPECT_EQ(24, GetUint64AlignedStringLength("0123456789abcdef"));
}

TEST(PerfDataUtilsTest, PerfizeBuildID) {
  string build_id_string = "f";
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("f000000000000000000000000000000000000000", build_id_string);
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("f000000000000000000000000000000000000000", build_id_string);

  build_id_string = "01234567890123456789012345678901234567890";
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("0123456789012345678901234567890123456789", build_id_string);
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("0123456789012345678901234567890123456789", build_id_string);
}

TEST(PerfDataUtilsTest, UnperfizeBuildID) {
  string build_id_string = "f000000000000000000000000000000000000000";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("f0000000", build_id_string);
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("f0000000", build_id_string);

  build_id_string = "0123456789012345678901234567890123456789";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("0123456789012345678901234567890123456789", build_id_string);

  build_id_string = "0000000000000000000000000000001000000000";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("00000000000000000000000000000010", build_id_string);
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("00000000000000000000000000000010", build_id_string);

  build_id_string = "0000000000000000000000000000000000000000";  // 40 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);

  build_id_string = "00000000000000000000000000000000";  // 32 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);

  build_id_string = "00000000";  // 8 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);

  build_id_string = "0000000";  // 7 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("0000000", build_id_string);

  build_id_string = "";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);
}

}  // namespace quipper
