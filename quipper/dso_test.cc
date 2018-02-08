// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dso.h"

#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "base/logging.h"
#include "buffer_reader.h"
#include "compat/string.h"
#include "compat/test.h"
#include "dso_test_utils.h"
#include "scoped_temp_path.h"

namespace quipper {

TEST(DsoTest, ReadsBuildId) {
  InitializeLibelf();
  ScopedTempFile elf("/tmp/tempelf.");

  const string expected_buildid = "\xde\xad\xf0\x0d";
  testing::WriteElfWithBuildid(elf.path(), ".note.gnu.build-id",
                               expected_buildid);

  string buildid;
  EXPECT_TRUE(ReadElfBuildId(elf.path(), &buildid));
  EXPECT_EQ(expected_buildid, buildid);

  // Repeat with other spellings of the section name:

  testing::WriteElfWithBuildid(elf.path(), ".notes", expected_buildid);
  EXPECT_TRUE(ReadElfBuildId(elf.path(), &buildid));
  EXPECT_EQ(expected_buildid, buildid);

  testing::WriteElfWithBuildid(elf.path(), ".note", expected_buildid);
  EXPECT_TRUE(ReadElfBuildId(elf.path(), &buildid));
  EXPECT_EQ(expected_buildid, buildid);
}

TEST(DsoTest, ReadsBuildId_MissingBuildid) {
  InitializeLibelf();
  ScopedTempFile elf("/tmp/tempelf.");

  testing::WriteElfWithMultipleBuildids(elf.path(), {/*empty*/});

  string buildid;
  EXPECT_FALSE(ReadElfBuildId(elf.path(), &buildid));
}

TEST(DsoTest, ReadsBuildId_WrongSection) {
  InitializeLibelf();
  ScopedTempFile elf("/tmp/tempelf.");

  testing::WriteElfWithBuildid(elf.path(), ".unexpected-section", "blah");

  string buildid;
  EXPECT_FALSE(ReadElfBuildId(elf.path(), &buildid));
}

TEST(DsoTest, ReadsBuildId_PrefersGnuBuildid) {
  InitializeLibelf();
  ScopedTempFile elf("/tmp/tempelf.");

  const string buildid_gnu = "\xde\xad\xf0\x0d";
  const string buildid_notes = "\xc0\xde\xf0\x0d";
  const string buildid_note = "\xfe\xed\xba\xad";

  std::vector<std::pair<string, string>> section_buildids{
      std::make_pair(".notes", buildid_notes),
      std::make_pair(".note", buildid_note),
      std::make_pair(".note.gnu.build-id", buildid_gnu),
  };
  testing::WriteElfWithMultipleBuildids(elf.path(), section_buildids);

  string buildid;
  EXPECT_TRUE(ReadElfBuildId(elf.path(), &buildid));
  EXPECT_EQ(buildid_gnu, buildid);

  // Also prefer ".notes" over ".note"
  section_buildids = {
      std::make_pair(".note", buildid_note),
      std::make_pair(".notes", buildid_notes),
  };
  testing::WriteElfWithMultipleBuildids(elf.path(), section_buildids);

  EXPECT_TRUE(ReadElfBuildId(elf.path(), &buildid));
  EXPECT_EQ(buildid_notes, buildid);
}

TEST(DsoTest, ReadsSysfsModuleBuildidNote) {
  // Mimic contents of a /sys/module/<name>/notes/.note.gnu.build-id file.
  const size_t namesz = 4;
  const size_t descsz = 0x14;
  const GElf_Nhdr note_header = {
      .n_namesz = namesz,
      .n_descsz = descsz,
      .n_type = NT_GNU_BUILD_ID,
  };

  const char note_name[namesz] = ELF_NOTE_GNU;
  const char note_desc[descsz]{
      // Note \0 here. This is not null-terminated.
      '\x1c', '\0',   '\x69', '\x27', '\x15', '\x26', '\x6b',
      '\xe7', '\xcc', '\x69', '\x2c', '\x12', '\xe8', '\x09',
      '\x20', '\x18', '\x03', '\x5b', '\xb6', '\x4f',
  };

  string data;
  data.append(reinterpret_cast<const char*>(&note_header), sizeof(note_header));
  data.append(note_name, sizeof(note_name));
  data.append(note_desc, sizeof(note_desc));

  ASSERT_EQ(0x24, data.size()) << "Sanity";

  BufferReader data_reader(data.data(), data.size());
  string buildid;
  EXPECT_TRUE(ReadBuildIdNote(&data_reader, &buildid));
  EXPECT_EQ(string(note_desc, sizeof(note_desc)), buildid);
}

}  // namespace quipper
