// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binary_data_utils.h"
#include "compat/test.h"

namespace quipper {

namespace {

const size_t kHexArraySize = 8;

}  // namespace

TEST(BinaryDataUtilsTest, MD5) {
  ASSERT_EQ(Md5Prefix(""), 0xd41d8cd98f00b204LL);
  ASSERT_EQ(Md5Prefix("The quick brown fox jumps over the lazy dog."),
            0xe4d909c290d0fb1cLL);
}

TEST(BinaryDataUtilsTest, Align) {
  EXPECT_EQ(12, Align<4>(10));
  EXPECT_EQ(12, Align<4>(12));
  EXPECT_EQ(16, Align<4>(13));
  EXPECT_EQ(100, Align<4>(97));
  EXPECT_EQ(100, Align<4>(100));
  EXPECT_EQ(104, Align<8>(100));
  EXPECT_EQ(112, Align<8>(108));
  EXPECT_EQ(112, Align<8>(112));

  EXPECT_EQ(12, Align<uint32_t>(10));
  EXPECT_EQ(112, Align<uint64_t>(112));
}

TEST(BinaryDataUtilsTest, RawDataToHexString) {
  u8 hex_number[kHexArraySize];
  // Generate a sequence of bytes and check its hex string representation.
  for (size_t i = 0; i < arraysize(hex_number); ++i) hex_number[i] = i << i;
  EXPECT_EQ("0002081840a08080",
            RawDataToHexString(hex_number, arraysize(hex_number)));

  // Change the first and last bytes and check the new hex string.
  hex_number[0] = 0x8f;
  hex_number[arraysize(hex_number) - 1] = 0x64;
  EXPECT_EQ("8f02081840a08064",
            RawDataToHexString(hex_number, arraysize(hex_number)));
}

TEST(BinaryDataUtilsTest, StringToHex) {
  u8 output[kHexArraySize], expected[kHexArraySize];

  // Use the same tests as in RawDataToHexString, except reversed.
  for (size_t i = 0; i < arraysize(expected); ++i) expected[i] = i << i;
  EXPECT_TRUE(
      HexStringToRawData("0002081840a08080", output, arraysize(output)));
  for (size_t i = 0; i < arraysize(expected); ++i)
    EXPECT_EQ(expected[i], output[i]);

  expected[0] = 0x8f;
  expected[arraysize(expected) - 1] = 0x64;
  EXPECT_TRUE(HexStringToRawData("8f02081840a080640123456789abcdef", output,
                                 arraysize(output)));
  for (size_t i = 0; i < arraysize(expected); ++i)
    EXPECT_EQ(expected[i], output[i]);
}

}  // namespace quipper
