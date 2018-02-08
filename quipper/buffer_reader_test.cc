// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <algorithm>
#include <vector>

#include "buffer_reader.h"
#include "compat/string.h"
#include "compat/test.h"

namespace quipper {

// Move the cursor around and make sure the offset is properly set each time.
TEST(BufferReaderTest, MoveOffset) {
  const std::vector<uint8_t> input(1000);
  BufferReader reader(input.data(), input.size());

  // Make sure the reader was properly created.
  EXPECT_EQ(input.size(), reader.size());
  EXPECT_EQ(0, reader.Tell());

  // Move the read cursor around.
  reader.SeekSet(100);
  EXPECT_EQ(100, reader.Tell());
  reader.SeekSet(900);
  EXPECT_EQ(900, reader.Tell());
  reader.SeekSet(500);
  EXPECT_EQ(500, reader.Tell());

  // The cursor can be set to past the end of the buffer, but can't perform any
  // read operations there.
  reader.SeekSet(1200);
  EXPECT_EQ(1200, reader.Tell());
  int dummy;
  EXPECT_FALSE(reader.ReadData(sizeof(dummy), &dummy));
}

// Make sure that the reader can handle a read size of zero.
TEST(BufferReaderTest, ReadZeroBytes) {
  const std::vector<uint8_t> input(10);
  BufferReader reader(input.data(), input.size());

  // Move to some location within the buffer.
  reader.SeekSet(5);
  EXPECT_TRUE(reader.ReadData(0, NULL));

  // Make sure the read pointer hasn't moved.
  EXPECT_EQ(5, reader.Tell());
}

// Read in all data from the input buffer at once.
TEST(BufferReaderTest, ReadSingleChunk) {
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  BufferReader reader(kInputData.data(), kInputData.size());

  std::vector<uint8_t> output(kInputData.size());
  EXPECT_TRUE(reader.ReadData(output.size(), output.data()));
  EXPECT_EQ(output.size(), reader.Tell());
  // Compare input and output data, converting the latter to a string for
  // clarity of error messages.
  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Test the ReadDataValue() function, which is a wrapper around ReadData().
TEST(BufferReaderTest, ReadDataValue) {
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  BufferReader reader(kInputData.data(), kInputData.size());

  std::vector<uint8_t> output(kInputData.size());
  EXPECT_TRUE(reader.ReadDataValue(output.size(), "data", output.data()));
  EXPECT_EQ(output.size(), reader.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Read in all data from the input buffer in multiple chunks, in order.
TEST(BufferReaderTest, ReadMultipleChunks) {
  // This string is 26 characters long.
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  BufferReader reader(kInputData.data(), kInputData.size());

  // Make sure the cursor is updated after each read.
  std::vector<uint8_t> output(kInputData.size());
  EXPECT_TRUE(reader.ReadData(10, output.data() + reader.Tell()));
  EXPECT_EQ(10, reader.Tell());
  EXPECT_TRUE(reader.ReadData(5, output.data() + reader.Tell()));
  EXPECT_EQ(15, reader.Tell());
  EXPECT_TRUE(reader.ReadData(5, output.data() + reader.Tell()));
  EXPECT_EQ(20, reader.Tell());
  EXPECT_TRUE(reader.ReadData(6, output.data() + reader.Tell()));
  EXPECT_EQ(26, reader.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Read in all data from the input buffer in multiple chunks, but not in order.
TEST(BufferReaderTest, ReadWithJumps) {
  // This string contains four parts, each 10 characters long.
  const string kInputData =
      "0:abcdefg;"
      "1:hijklmn;"
      "2:opqrstu;"
      "3:vwxyzABC";
  BufferReader reader(kInputData.data(), kInputData.size());

  // Read the data in multiple operations, but not in order. Overwrite the
  // previously read data in the destination buffer during each read.
  std::vector<uint8_t> output(10);

  reader.SeekSet(20);
  EXPECT_TRUE(reader.ReadData(10, output.data()));
  EXPECT_EQ(30, reader.Tell());
  EXPECT_EQ("2:opqrstu;", string(output.begin(), output.end()));

  reader.SeekSet(10);
  EXPECT_TRUE(reader.ReadData(10, output.data()));
  EXPECT_EQ(20, reader.Tell());
  EXPECT_EQ("1:hijklmn;", string(output.begin(), output.end()));

  reader.SeekSet(30);
  EXPECT_TRUE(reader.ReadData(10, output.data()));
  EXPECT_EQ(40, reader.Tell());
  EXPECT_EQ("3:vwxyzABC", string(output.begin(), output.end()));

  reader.SeekSet(0);
  EXPECT_TRUE(reader.ReadData(10, output.data()));
  EXPECT_EQ(10, reader.Tell());
  EXPECT_EQ("0:abcdefg;", string(output.begin(), output.end()));
}

// Test reading past the end of the buffer.
TEST(BufferReaderTest, ReadPastEndOfData) {
  // This string is 26 characters long.
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";
  BufferReader reader(kInputData.data(), kInputData.size());

  // Must not be able to read past the end of the buffer.
  std::vector<uint8_t> output(kInputData.size());
  reader.SeekSet(0);
  EXPECT_FALSE(reader.ReadData(30, output.data()));
  // The read pointer should not have moved.
  EXPECT_EQ(0, reader.Tell());

  // Should still be able to read within the bounds of the buffer, despite the
  // out-of-bounds read earlier.
  EXPECT_TRUE(reader.ReadData(13, output.data()));
  EXPECT_EQ(13, reader.Tell());

  // Now attempt another read past the end of the buffer, but starting from the
  // ending position of the previous read operation.
  EXPECT_FALSE(reader.ReadData(20, output.data() + reader.Tell()));
  // The read pointer should be unchanged.
  EXPECT_EQ(13, reader.Tell());

  // Read the rest of the data and make sure it matches the input.
  EXPECT_TRUE(reader.ReadData(13, output.data() + reader.Tell()));
  EXPECT_EQ(26, reader.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Test string reads.
TEST(BufferReaderTest, ReadString) {
  // Construct an input string.
  string input("The quick brown fox jumps over the lazy dog.");

  // Read the full string.
  BufferReader full_reader(input.data(), input.size());
  string full_reader_output;
  EXPECT_TRUE(full_reader.ReadString(input.size(), &full_reader_output));
  EXPECT_EQ(input.size(), full_reader.Tell());
  EXPECT_EQ(input, full_reader_output);

  // Read the full string plus the null pointer.
  BufferReader full_null_reader(input.data(), input.size() + 1);
  string full_null_reader_output;
  EXPECT_TRUE(
      full_null_reader.ReadString(input.size() + 1, &full_null_reader_output));
  EXPECT_EQ(input.size() + 1, full_null_reader.Tell());
  EXPECT_EQ(input, full_null_reader_output);

  // Read the first half of the string.
  BufferReader half_reader(input.data(), input.size() / 2);
  string half_reader_output;
  EXPECT_TRUE(half_reader.ReadString(input.size() / 2, &half_reader_output));
  EXPECT_EQ(input.size() / 2, half_reader.Tell());
  EXPECT_EQ(input.substr(0, input.size() / 2), half_reader_output);

  // Attempt to read past the end of the string.
  BufferReader past_end_reader(input.data(), input.size());
  string past_end_reader_output;
  EXPECT_FALSE(
      past_end_reader.ReadString(input.size() + 2, &past_end_reader_output));

  // Create a vector with some extra padding behind it. The padding should be
  // all zeroes. Read from this vector, with a size that encompasses the
  // padding.
  std::vector<char> input_vector(input.begin(), input.end());
  input_vector.resize(input.size() + 10, '\0');

  BufferReader vector_reader(input_vector.data(), input_vector.size());
  string vector_reader_output;
  EXPECT_TRUE(
      vector_reader.ReadString(input_vector.size(), &vector_reader_output));
  // The reader should have read past the padding too.
  EXPECT_EQ(input_vector.size(), vector_reader.Tell());
  EXPECT_EQ(input, vector_reader_output);
}

string MakeStringWithNullsForSpaces(string str) {
  std::replace(str.begin(), str.end(), ' ', '\0');
  return str;
}

TEST(BufferReaderTest, ReadDataString) {
  // Construct an input string.
  string input = MakeStringWithNullsForSpaces(
      "The quick brown fox jumps over the lazy dog.");

  BufferReader reader(input.data(), input.size());

  string expected_out;
  string out;
  out.resize(5);
  EXPECT_TRUE(reader.ReadDataString(10, &out));
  expected_out = MakeStringWithNullsForSpaces("The quick ");
  EXPECT_EQ(10, expected_out.size()) << "Sanity";
  EXPECT_EQ(expected_out, out);

  out.resize(15);
  EXPECT_TRUE(reader.ReadDataString(10, &out));
  expected_out = MakeStringWithNullsForSpaces("brown fox ");
  EXPECT_EQ(10, expected_out.size()) << "Sanity";
  EXPECT_EQ(expected_out, out);

  reader.SeekSet(reader.size());

  // Check destination contents don't get modified on failure.
  out.resize(5);
  expected_out = out;
  EXPECT_FALSE(reader.ReadDataString(10, &out));
  EXPECT_EQ(expected_out, out) << "Should be unchanged.";

  out.resize(15);
  expected_out = out;
  EXPECT_FALSE(reader.ReadDataString(10, &out));
  EXPECT_EQ(expected_out, out) << "Should be unchanged.";

  EXPECT_FALSE(out.empty()) << "Sanity";
  EXPECT_TRUE(reader.ReadDataString(0, &out));
  EXPECT_TRUE(out.empty()) << "Read zero-length should clear output.";
}

// Reads data to a buffer and verifies that the buffer has not been modified
// beyond the writable boundaries.
TEST(BufferReaderTest, NoWritingOutOfBounds) {
  // The input data contains all zeroes.
  std::vector<uint8_t> input(800, 0);
  BufferReader reader(input.data(), input.size());

  // A sentinel value that fills memory to detect when that section of memory is
  // overwritten. If the memory shows another value, it means it has been
  // overwritten.
  const char kUnwrittenValue = 0xaa;

  // Create a destination buffer filled with the above sentinel value.
  std::vector<uint8_t> buffer(1000, kUnwrittenValue);
  // Only write to the range [100, 900).
  EXPECT_TRUE(reader.ReadData(800, buffer.data() + 100));

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

}  // namespace quipper
