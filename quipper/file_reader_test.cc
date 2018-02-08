// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <vector>

#include "compat/test.h"
#include "file_reader.h"
#include "file_utils.h"
#include "scoped_temp_path.h"
#include "test_utils.h"

namespace quipper {

// Move the cursor around and make sure the offset is properly set each time.
TEST(FileReaderTest, MoveOffset) {
  std::vector<uint8_t> input_data(1000);

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), input_data));

  // Create a reader for reading.
  FileReader reader(input_file.path());
  EXPECT_EQ(input_data.size(), reader.size());
  EXPECT_EQ(0, reader.Tell());

  // Move the read cursor around.
  reader.SeekSet(100);
  EXPECT_EQ(100, reader.Tell());
  reader.SeekSet(900);
  EXPECT_EQ(900, reader.Tell());
  reader.SeekSet(500);
  EXPECT_EQ(500, reader.Tell());

  // The cursor can be set to past the end of the file, but can't perform any
  // read operations there.
  reader.SeekSet(1200);
  EXPECT_EQ(1200, reader.Tell());
  int dummy;
  EXPECT_FALSE(reader.ReadData(sizeof(dummy), &dummy));
}

// Make sure that the reader can handle a read size of zero.
TEST(FileReaderTest, ReadZeroBytes) {
  std::vector<uint8_t> input_data(10);

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), input_data));

  FileReader reader(input_file.path());
  reader.SeekSet(5);
  EXPECT_TRUE(reader.ReadData(0, NULL));

  // Make sure the read pointer hasn't moved.
  EXPECT_EQ(5, reader.Tell());
}

// Read in all data from the input file at once.
TEST(FileReaderTest, ReadSingleChunk) {
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), kInputData));
  FileReader reader(input_file.path());

  // Read all the data from the file in one go.
  std::vector<uint8_t> output(kInputData.size());
  EXPECT_TRUE(reader.ReadData(output.size(), output.data()));
  EXPECT_EQ(output.size(), reader.Tell());
  // Compare input and output data, converting the latter to a string for
  // clarity of error messages.
  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Test the ReadDataValue() function, which is a wrapper around ReadData().
TEST(FileReaderTest, ReadDataValue) {
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), kInputData));
  FileReader reader(input_file.path());

  // Read all the data from the file in one go.
  std::vector<uint8_t> output(kInputData.size());
  EXPECT_TRUE(reader.ReadDataValue(output.size(), "data", output.data()));
  EXPECT_EQ(output.size(), reader.Tell());

  EXPECT_EQ(kInputData, string(output.begin(), output.end()));
}

// Read in all data from the input file in multiple chunks, in order.
TEST(FileReaderTest, ReadMultipleChunks) {
  // This string is 26 characters long.
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), kInputData));
  FileReader reader(input_file.path());

  // Read the data in multiple operations. Make sure the cursor is updated.
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

// Read in all data from the input file in multiple chunks, but not in order.
TEST(FileReaderTest, ReadWithJumps) {
  // This string contains four parts, each 10 characters long.
  const string kInputData =
      "0:abcdefg;"
      "1:hijklmn;"
      "2:opqrstu;"
      "3:vwxyzABC";

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), kInputData));
  FileReader reader(input_file.path());

  // Read the data in multiple operations, but not in order. The destination
  // offset must still match the source offset.
  std::vector<uint8_t> output(10);

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

// Test reading past the end of the file.
TEST(FileReaderTest, ReadPastEndOfData) {
  // This string is 26 characters long.
  const string kInputData = "abcdefghijklmnopqrstuvwxyz";

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), kInputData));
  FileReader reader(input_file.path());

  // Must not be able to read past the end of the file.
  std::vector<uint8_t> output(kInputData.size());
  reader.SeekSet(0);
  EXPECT_FALSE(reader.ReadData(30, output.data()));
  // The read pointer should not have moved.
  EXPECT_EQ(0, reader.Tell());

  // Should still be able to read within the bounds of the file, despite the
  // out-of-bounds read earlier.
  EXPECT_TRUE(reader.ReadData(13, output.data()));
  EXPECT_EQ(13, reader.Tell());

  // Now attempt another read past the end of the file, but starting from the
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
TEST(FileReaderTest, ReadString) {
  // Construct an input string.
  string input_string("The quick brown fox jumps over the lazy dog.");

  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), input_string));

  // Read the full string.
  FileReader full_reader(input_file.path());
  string full_reader_output;
  EXPECT_TRUE(full_reader.ReadString(input_string.size(), &full_reader_output));
  EXPECT_EQ(input_string.size(), full_reader.Tell());
  EXPECT_EQ(input_string, full_reader_output);

  // Read the first half of the string.
  FileReader half_reader(input_file.path());
  string half_reader_output;
  EXPECT_TRUE(
      half_reader.ReadString(input_string.size() / 2, &half_reader_output));
  EXPECT_EQ(input_string.size() / 2, half_reader.Tell());
  EXPECT_EQ(input_string.substr(0, input_string.size() / 2),
            half_reader_output);

  // Attempt to read past the end of the string.
  FileReader past_end_reader(input_file.path());
  string past_end_reader_output = "previous string value";
  EXPECT_FALSE(past_end_reader.ReadString(input_string.size() + 1,
                                          &past_end_reader_output));
  EXPECT_EQ("previous string value", past_end_reader_output);

  // Create a string with some extra padding behind it. The padding should be
  // all zeroes. Read from this string, with a size that encompasses the
  // padding.
  string input_string_with_padding(input_string.begin(), input_string.end());
  input_string_with_padding.resize(input_string.size() + 10, '\0');

  ScopedTempFile input_file_padded;
  ASSERT_TRUE(
      BufferToFile(input_file_padded.path(), input_string_with_padding));

  // Read everything including the padding.
  FileReader padding_reader(input_file_padded.path());
  string padding_reader_output;
  EXPECT_TRUE(padding_reader.ReadString(input_string_with_padding.size(),
                                        &padding_reader_output));
  // The reader should have read past the padding too.
  EXPECT_EQ(input_string_with_padding.size(), padding_reader.Tell());
  // However, the output string itself should not have padding.
  EXPECT_EQ(input_string, padding_reader_output);
}

// Reads data to a buffer and verifies that the buffer has not been modified
// beyond the writable boundaries.
TEST(FileReaderTest, NoWritingOutOfBounds) {
  // The input data contains all zeroes.
  std::vector<uint8_t> input_data(800, 0);

  // Write it to file.
  ScopedTempFile input_file;
  ASSERT_TRUE(BufferToFile(input_file.path(), input_data));
  FileReader reader(input_file.path());

  // A sentinel value that fills memory to detect when that section of memory is
  // overwritten. If the memory shows another value, it means it has been
  // overwritten.
  const char kUnwrittenValue = 0xaa;

  // Create a destination buffer filled with the above sentinel value.
  std::vector<uint8_t> buffer(1000, kUnwrittenValue);
  // Only write to the range [100, 900).
  EXPECT_TRUE(reader.ReadData(800, buffer.data() + 100));

  // Check that the data was written to the writable part of the buffer.
  EXPECT_EQ(input_data,
            std::vector<uint8_t>(buffer.begin() + 100, buffer.begin() + 900));

  // Now make sure that the other parts of the buffer haven't been overwritten.
  const std::vector<uint8_t> expected_unwritten_part(100, kUnwrittenValue);
  EXPECT_EQ(expected_unwritten_part,
            std::vector<uint8_t>(buffer.begin(), buffer.begin() + 100));
  EXPECT_EQ(expected_unwritten_part,
            std::vector<uint8_t>(buffer.begin() + 900, buffer.begin() + 1000));
}

}  // namespace quipper
