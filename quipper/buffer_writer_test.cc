// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <vector>

#include "buffer_writer.h"
#include "compat/test.h"

namespace quipper {

// Move the cursor around and make sure the offset is properly set each time.
TEST(BufferWriterTest, MoveOffset) {
  std::vector<uint8_t> buffer(1000);

  BufferWriter writer(buffer.data(), buffer.size());
  EXPECT_EQ(0, writer.Tell());
  EXPECT_EQ(buffer.size(), writer.size());

  // Move the write cursor around.
  writer.SeekSet(100);
  EXPECT_EQ(100, writer.Tell());
  writer.SeekSet(900);
  EXPECT_EQ(900, writer.Tell());
  writer.SeekSet(500);
  EXPECT_EQ(500, writer.Tell());

  // The cursor can be set to past the end of the buffer, but can't perform any
  // write operations there.
  writer.SeekSet(1200);
  EXPECT_EQ(1200, writer.Tell());
  int dummy = 0;
  EXPECT_FALSE(writer.WriteData(&dummy, sizeof(dummy)));
}

// Make sure that the writer can handle a write size of zero.
TEST(BufferWriterTest, WriteZeroBytes) {
  std::vector<uint8_t> output(10);
  BufferWriter writer(output.data(), output.size());
  writer.SeekSet(5);
  EXPECT_TRUE(writer.WriteData(NULL, 0));
  // Make sure the write pointer hasn't moved.
  EXPECT_EQ(5, writer.Tell());
}

// Write a chunk of data to the output buffer.
TEST(BufferWriterTest, WriteSingleChunk) {
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  std::vector<uint8_t> output(kInputData.size());
  BufferWriter writer(output.data(), output.size());

  EXPECT_TRUE(writer.WriteData(kInputData.data(), kInputData.size()));
  EXPECT_EQ(output.size(), writer.Tell());

  // Compare input and output data, converting the latter to a string for
  // clarity of error messages.
  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Test the WriteDataValue() function, which is a wrapper around WriteData().
TEST(BufferWriterTest, WriteDataValue) {
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  std::vector<uint8_t> output(kInputData.size());
  BufferWriter writer(output.data(), output.size());

  EXPECT_TRUE(
      writer.WriteDataValue(kInputData.data(), kInputData.size(), "data"));
  EXPECT_EQ(output.size(), writer.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Write in all data from the input buffer in multiple chunks, in order.
TEST(BufferWriterTest, WriteMultipleChunks) {
  // This string is 26 characters long.
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";

  std::vector<uint8_t> output(kInputData.size());
  BufferWriter writer(output.data(), output.size());

  // Write all the data in multiple operations. Make sure the cursor is updated.
  EXPECT_TRUE(writer.WriteData(kInputData.data() + writer.Tell(), 10));
  EXPECT_EQ(10, writer.Tell());
  EXPECT_TRUE(writer.WriteData(kInputData.data() + writer.Tell(), 5));
  EXPECT_EQ(15, writer.Tell());
  EXPECT_TRUE(writer.WriteData(kInputData.data() + writer.Tell(), 5));
  EXPECT_EQ(20, writer.Tell());
  EXPECT_TRUE(writer.WriteData(kInputData.data() + writer.Tell(), 6));
  EXPECT_EQ(26, writer.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Write all data from the input buffer in multiple chunks, but not in order.
TEST(BufferWriterTest, WriteWithJumps) {
  // This string contains four parts, each 10 characters long.
  const string kInputData =
      "0:abcdefg;"
      "1:hijklmn;"
      "2:opqrstu;"
      "3:vwxyzABC";

  std::vector<uint8_t> output(kInputData.size());
  BufferWriter writer(output.data(), output.size());

  writer.SeekSet(20);
  EXPECT_TRUE(writer.WriteData(kInputData.data() + 20, 10));
  EXPECT_EQ(30, writer.Tell());
  EXPECT_EQ("2:opqrstu;", string(output.begin() + 20, output.begin() + 30));

  writer.SeekSet(10);
  EXPECT_TRUE(writer.WriteData(kInputData.data() + 10, 10));
  EXPECT_EQ(20, writer.Tell());
  EXPECT_EQ("1:hijklmn;", string(output.begin() + 10, output.begin() + 20));

  writer.SeekSet(30);
  EXPECT_TRUE(writer.WriteData(kInputData.data() + 30, 10));
  EXPECT_EQ(40, writer.Tell());
  EXPECT_EQ("3:vwxyzABC", string(output.begin() + 30, output.begin() + 40));

  writer.SeekSet(0);
  EXPECT_TRUE(writer.WriteData(kInputData.data(), 10));
  EXPECT_EQ(10, writer.Tell());
  EXPECT_EQ("0:abcdefg;", string(output.begin(), output.begin() + 10));
}

// Test writing past the end of the buffer.
TEST(BufferWriterTest, WritePastEndOfData) {
  // This string is 26 characters long.
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  std::vector<uint8_t> output(kInputData.size());
  BufferWriter writer(output.data(), output.size());

  // Must not be able to write past the end of the buffer.
  writer.SeekSet(0);
  EXPECT_FALSE(writer.WriteData(kInputData.data() + writer.Tell(), 30));
  // The write pointer should not have moved.
  EXPECT_EQ(0, writer.Tell());

  // Should still be able to write within the bounds of the buffer, despite the
  // out-of-bounds write earlier.
  EXPECT_TRUE(writer.WriteData(kInputData.data() + writer.Tell(), 13));
  EXPECT_EQ(13, writer.Tell());

  // Now attempt another write past the end of the buffer, but starting from the
  // ending position of the previous write operation.
  EXPECT_FALSE(writer.WriteData(kInputData.data() + writer.Tell(), 20));
  // The write pointer should be unchanged.
  EXPECT_EQ(13, writer.Tell());

  // Write the rest of the data and make sure it matches the input.
  EXPECT_TRUE(writer.WriteData(kInputData.data() + writer.Tell(), 13));
  EXPECT_EQ(26, writer.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Test string writes.
TEST(BufferWriterTest, WriteString) {
  // Construct an input string.
  string input("The quick brown fox jumps over the lazy dog.");

  // Write the full string.
  std::vector<char> full_output(input.size());
  BufferWriter full_writer(full_output.data(), full_output.size());
  EXPECT_TRUE(full_writer.WriteString(input, input.size()));
  EXPECT_EQ(input.size(), full_writer.Tell());
  // There is no null pointer at the end of the output buffer, so create a
  // string out of it using the known length of the input string.
  EXPECT_EQ(input, string(full_output.data(), input.size()));

  // Write the full string plus the null pointer.
  std::vector<char> full_null_output(input.size() + 1);
  BufferWriter full_null_writer(full_null_output.data(),
                                full_null_output.size());
  EXPECT_TRUE(full_null_writer.WriteString(input, input.size() + 1));
  EXPECT_EQ(input.size() + 1, full_null_writer.Tell());
  // The null pointer should have been written. It should determine the end of
  // the string.
  EXPECT_EQ(input, string(full_null_output.data()));

  // Write the first half of the string.
  std::vector<char> half_output(input.size() / 2);
  BufferWriter half_writer(half_output.data(), half_output.size());
  EXPECT_TRUE(half_writer.WriteString(input, input.size() / 2));
  EXPECT_EQ(input.size() / 2, half_writer.Tell());
  // Null terminator is not guaranteed, so use the input string size to limit
  // the output string during comparison.
  EXPECT_EQ(input.substr(0, input.size() / 2),
            string(half_output.data(), input.size() / 2));

  // Attempt to write past the end of the buffer. Should fail.
  std::vector<char> past_end_buffer(input.size());
  BufferWriter past_end_writer(past_end_buffer.data(), past_end_buffer.size());
  EXPECT_FALSE(past_end_writer.WriteString(input, input.size() + 2));

  // Write string with some extra padding.
  std::vector<char> extra_padding_output(input.size() + 10);
  BufferWriter vector_writer(extra_padding_output.data(),
                             extra_padding_output.size());
  EXPECT_TRUE(vector_writer.WriteString(input, extra_padding_output.size()));
  // The writer should have written both the string data and padding bytes.
  EXPECT_EQ(extra_padding_output.size(), vector_writer.Tell());
  // But the string should still be null-terminated.
  EXPECT_EQ(input, extra_padding_output.data());
}

// Writes data to a buffer and verifies that the buffer has not been modified
// beyond the writable boundaries.
TEST(BufferWriterTest, NoWritingOutOfBounds) {
  // A sentinel value that fills memory to detect when that section of memory is
  // overwritten. If the memory shows another value, it means it has been
  // overwritten.
  const uint8_t kUnwrittenValue = 0xaa;

  std::vector<uint8_t> buffer(1000, kUnwrittenValue);
  // Only write to the range [100, 900).
  BufferWriter writer(buffer.data() + 100, 800);

  // Create some input data that's filled with zeroes. Write this to the buffer.
  std::vector<uint8_t> input(800, 0);
  EXPECT_TRUE(writer.WriteData(input.data(), input.size()));
  EXPECT_EQ(input.size(), writer.Tell());

  // Check that the data was written to the writable part of the buffer.
  EXPECT_EQ(input,
            std::vector<uint8_t>(buffer.begin() + 100, buffer.begin() + 900));

  // Now make sure that the other parts of the buffer haven't been overwritten.
  const std::vector<uint8_t> expected_unwritten_part(100, kUnwrittenValue);
  EXPECT_EQ(expected_unwritten_part,
            std::vector<uint8_t>(buffer.begin(), buffer.begin() + 100));
  EXPECT_EQ(expected_unwritten_part,
            std::vector<uint8_t>(buffer.begin() + 900, buffer.begin() + 1000));
}

// Writes a string to a buffer and verifies that the buffer has not been
// modified beyond the writable boundaries.
TEST(BufferWriterTest, NoWritingStringOutOfBounds) {
  // Construct an input string.
  string input("This line is forty characters long.....");

  // A sentinel value that fills memory to detect when that section of memory is
  // overwritten. If the memory shows another value, it means it has been
  // overwritten.
  const uint8_t kUnwrittenValue = 0xaa;
  std::vector<char> buffer(100, kUnwrittenValue);

  // Only write to the range [20, 61). This includes enough space for the string
  // and the null terminator. Make sure the string is short enough that it fits
  // inside the buffer and leaves at least one byte of extra space at the end,
  // beyond the null terminator.
  ASSERT_LT(input.size() + 1, buffer.size() - 20);
  BufferWriter writer(buffer.data() + 20, input.size() + 1);

  // Write the string plus null terminator, and verify that it was written.
  EXPECT_TRUE(writer.WriteString(input, input.size() + 1));
  EXPECT_EQ(input, buffer.data() + 20);
  EXPECT_EQ(input.size() + 1, writer.Tell());

  // Now make sure that the other parts of the buffer haven't been overwritten.
  EXPECT_EQ(std::vector<char>(20, kUnwrittenValue),
            std::vector<char>(buffer.begin(), buffer.begin() + 20));
  // There are 39 bytes between offset 61 (end of the string contents) and the
  // end of the buffer.
  EXPECT_EQ(std::vector<char>(39, kUnwrittenValue),
            std::vector<char>(buffer.begin() + 61, buffer.end()));
}

}  // namespace quipper
