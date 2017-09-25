// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dso_test_utils.h"

#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "base/logging.h"
#include "binary_data_utils.h"

namespace quipper {
namespace testing {

namespace {

// Example string table:
//   index  0: "\0"
//   index  1: "value 1" "\0"
//   index  9: "value 2" "\0"
//   index 17: "value 3" "\0"
class ElfStringTable {
 public:
  ElfStringTable() : table_("", 1) {}  // index 0 is the empty string.
  // Returns the index to use in place of the string.
  GElf_Word Add(string value) {
    GElf_Word ret = table_.size();
    table_.append(value.data(), value.size()+1);  // Include the '\0'.
    return ret;
  }

  const string &table() { return table_; }

 private:
  string table_;
};

// Helper to control the scope of data in Elf_Data.d_buf added to a Elf_Scn.
// Basically, makes sure that the d_buf pointer remains valid, and holds on to a
// copy of your buffer so you don't have to.
class ElfDataCache {
 public:
  Elf_Data *AddDataToSection(Elf_Scn *section, const string &data_str) {
    Elf_Data *data = elf_newdata(section);
    CHECK(data) << elf_errmsg(-1);
    // avoid zero memory allocation
    char *data_storage = new char[data_str.size() + 1];
    cache_.emplace_back(data_storage);
    memcpy(data_storage, data_str.data(), data_str.size());
    data->d_buf = data_storage;
    data->d_size = data_str.size();
    return data;
  }

  ~ElfDataCache() {
    for (auto &s : cache_) {
      delete[] s;
    }
  }

 private:
  std::vector<char *> cache_;
};

}  // namespace

void WriteElfWithBuildid(string filename, string section_name, string buildid) {
  std::vector<std::pair<string, string>> section_name_to_buildid {
    std::make_pair(section_name, buildid)
  };
  WriteElfWithMultipleBuildids(filename, section_name_to_buildid);
}

void WriteElfWithMultipleBuildids(
    string filename,
    const std::vector<std::pair<string, string>> section_buildids) {
  int fd = open(filename.data(), O_WRONLY|O_CREAT|O_TRUNC, 0660);
  CHECK_GE(fd, 0) << strerror(errno);

  Elf *elf = elf_begin(fd, ELF_C_WRITE, nullptr);
  CHECK(elf) << elf_errmsg(-1);
  Elf64_Ehdr* elf_header = elf64_newehdr(elf);
  CHECK(elf_header) << elf_errmsg(-1);
  elf_header->e_ident[EI_DATA] = ELFDATA2LSB;
  elf_header->e_machine = EM_X86_64;
  elf_header->e_version = EV_CURRENT;
  CHECK(elf_update(elf, ELF_C_NULL) > 0) << elf_errmsg(-1);

  ElfStringTable string_table;
  ElfDataCache data_cache;

  // Note section(s)
  for (const auto& entry : section_buildids) {
    const string& section_name = entry.first;
    const string& buildid = entry.second;
    Elf_Scn *section = elf_newscn(elf);
    CHECK(section) << elf_errmsg(-1);
    GElf_Shdr section_header;
    CHECK(gelf_getshdr(section, &section_header)) << elf_errmsg(-1);
    section_header.sh_name = string_table.Add(section_name);
    section_header.sh_type = SHT_NOTE;
    CHECK(gelf_update_shdr(section, &section_header)) << elf_errmsg(-1);

    string note_name = ELF_NOTE_GNU;
    GElf_Nhdr note_header;
    note_header.n_namesz = Align<4>(note_name.size());
    note_header.n_descsz = Align<4>(buildid.size());
    note_header.n_type = NT_GNU_BUILD_ID;
    string data_str;
    data_str.append(reinterpret_cast<char*>(&note_header), sizeof(note_header));
    data_str.append(note_name);
    data_str.append(string(note_header.n_namesz - note_name.size(), '\0'));
    data_str.append(buildid);
    data_str.append(string(note_header.n_descsz - buildid.size(), '\0'));
    Elf_Data *data = data_cache.AddDataToSection(section, data_str);
    data->d_type = ELF_T_NHDR;
  }

  // String table section
  {
    Elf_Scn *section = elf_newscn(elf);
    CHECK(section) << elf_errmsg(-1);
    GElf_Shdr section_header;
    CHECK(gelf_getshdr(section, &section_header)) << elf_errmsg(-1);
    section_header.sh_name = string_table.Add(".shstrtab");
    section_header.sh_type = SHT_STRTAB;
    CHECK(gelf_update_shdr(section, &section_header)) << elf_errmsg(-1);
    Elf_Data *data = data_cache.AddDataToSection(section, string_table.table());
    data->d_type = ELF_T_BYTE;

    elf_header->e_shstrndx = elf_ndxscn(section);
  }

  CHECK(elf_update(elf, ELF_C_WRITE) > 0) << elf_errmsg(-1);
  elf_end(elf);

  close(fd);
}

}  // namespace testing
}  // namespace quipper
