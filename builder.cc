/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builder.h"

#include <fcntl.h>
#include <unistd.h>

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>
#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

using google::protobuf::io::StringOutputStream;
using google::protobuf::io::GzipOutputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::RepeatedField;

namespace perftools {
namespace profiles {

Builder::Builder() : profile_(new Profile()) {
  // string_table[0] must be ""
  profile_->add_string_table("");
}

int64 Builder::StringId(const char *str) {
  if (str == nullptr || !str[0]) {
    return 0;
  }

  const int64 index = profile_->string_table_size();
  const auto inserted = strings_.emplace(str, index);
  if (!inserted.second) {
    // Failed to insert -- use existing id.
    return inserted.first->second;
  }
  profile_->add_string_table(inserted.first->first);
  return index;
}

uint64 Builder::FunctionId(const char *name, const char *system_name,
                           const char *file, int64 start_line) {
  int64 name_index = StringId(name);
  int64 system_name_index = StringId(system_name);
  int64 file_index = StringId(file);

  Function fn(name_index, system_name_index, file_index, start_line);

  int64 index = profile_->function_size() + 1;
  const auto inserted = functions_.insert(std::make_pair(fn, index));
  const bool insert_successful = inserted.second;
  if (!insert_successful) {
    const auto existing_function = inserted.first;
    return existing_function->second;
  }

  auto function = profile_->add_function();
  function->set_id(index);
  function->set_name(name_index);
  function->set_system_name(system_name_index);
  function->set_filename(file_index);
  function->set_start_line(start_line);
  return index;
}

bool Builder::Emit(string *output) {
  *output = "";
  if (!profile_ || !Finalize()) {
    return false;
  }
  return Marshal(*profile_, output);
}

bool Builder::Marshal(const Profile &profile, string *output) {
  *output = "";
  StringOutputStream stream(output);
  GzipOutputStream gzip_stream(&stream);
  if (!profile.SerializeToZeroCopyStream(&gzip_stream)) {
    std::cerr << "Failed to serialize to gzip stream";
    return false;
  }
  return gzip_stream.Close();
}

bool Builder::MarshalToFile(const Profile &profile, int fd) {
  FileOutputStream stream(fd);
  GzipOutputStream gzip_stream(&stream);
  if (!profile.SerializeToZeroCopyStream(&gzip_stream)) {
    std::cerr << "Failed to serialize to gzip stream";
    return false;
  }
  return gzip_stream.Close();
}

bool Builder::MarshalToFile(const Profile &profile, const char *filename) {
  int fd =
      TEMP_FAILURE_RETRY(open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0444));
  if (fd == -1) {
    return false;
  }
  int ret = MarshalToFile(profile, fd);
  close(fd);
  return ret;
}

// Returns a bool indicating if the profile is valid. It logs any
// errors it encounters.
bool Builder::CheckValid(const Profile &profile) {
  std::unordered_set<uint64> mapping_ids;
  for (const auto &mapping : profile.mapping()) {
    const int64 id = mapping.id();
    if (id != 0) {
      const bool insert_successful = mapping_ids.insert(id).second;
      if (!insert_successful) {
        std::cerr << "Duplicate mapping id: " << id;
        return false;
      }
    }
  }

  std::unordered_set<uint64> function_ids;
  for (const auto &function : profile.function()) {
    const int64 id = function.id();
    if (id != 0) {
      const bool insert_successful = function_ids.insert(id).second;
      if (!insert_successful) {
        std::cerr << "Duplicate function id: " << id;
        return false;
      }
    }
  }

  std::unordered_set<uint64> location_ids;
  for (const auto &location : profile.location()) {
    const int64 id = location.id();
    if (id != 0) {
      const bool insert_successful = location_ids.insert(id).second;
      if (!insert_successful) {
        std::cerr << "Duplicate location id: " << id;
        return false;
      }
    }
    const int64 mapping_id = location.mapping_id();
    if (mapping_id != 0 && mapping_ids.count(mapping_id) == 0) {
      std::cerr << "Missing mapping " << mapping_id << " from location " << id;
      return false;
    }
    for (const auto &line : location.line()) {
      int64 function_id = line.function_id();
      if (function_id != 0 && function_ids.count(function_id) == 0) {
        std::cerr << "Missing function " << function_id;
        return false;
      }
    }
  }

  int sample_type_len = profile.sample_type_size();
  if (sample_type_len == 0) {
    std::cerr << "No sample type specified";
    return false;
  }

  for (const auto &sample : profile.sample()) {
    if (sample.value_size() != sample_type_len) {
      std::cerr << "Found sample with " << sample.value_size()
                 << " values, expecting " << sample_type_len;
      return false;
    }
    for (uint64 location_id : sample.location_id()) {
      if (location_id == 0) {
        std::cerr << "Sample referencing location_id=0";
        return false;
      }

      if (location_ids.count(location_id) == 0) {
        std::cerr << "Missing location " << location_id;
        return false;
      }
    }

    for (const auto &label : sample.label()) {
      int64 str = label.str();
      int64 num = label.num();
      if (str != 0 && num != 0) {
        std::cerr << "One of str/num must be unset, got " << str << "," << num;
        return false;
      }
    }
  }
  return true;
}

// Finalizes the profile for serialization.
// - Creates missing locations for unsymbolized profiles.
// - Associates locations to the corresponding mappings.
bool Builder::Finalize() {
  if (profile_->location_size() == 0) {
    std::unordered_map<uint64, uint64> address_to_id;
    for (auto &sample : *profile_->mutable_sample()) {
      // Copy sample locations into a temp vector, and then clear and
      // repopulate it with the corresponding location IDs.
      const RepeatedField<uint64> addresses = sample.location_id();
      sample.clear_location_id();
      for (uint64 address : addresses) {
        int64 index = address_to_id.size() + 1;
        const auto inserted = address_to_id.emplace(address, index);
        if (inserted.second) {
          auto loc = profile_->add_location();
          loc->set_id(index);
          loc->set_address(address);
        }
        sample.add_location_id(inserted.first->second);
      }
    }
  }

  // Look up location address on mapping ranges.
  if (profile_->mapping_size() > 0) {
    std::map<uint64, std::pair<uint64, uint64> > mapping_map;
    for (const auto &mapping : profile_->mapping()) {
      mapping_map[mapping.memory_start()] =
          std::make_pair(mapping.memory_limit(), mapping.id());
    }

    for (auto &loc : *profile_->mutable_location()) {
      if (loc.address() != 0 && loc.mapping_id() == 0) {
        auto mapping = mapping_map.upper_bound(loc.address());
        if (mapping == mapping_map.begin()) {
          // Address landed before the first mapping
          continue;
        }
        mapping--;
        uint64 limit = mapping->second.first;
        uint64 id = mapping->second.second;

        if (loc.address() <= limit) {
          loc.set_mapping_id(id);
        }
      }
    }
  }
  return CheckValid(*profile_);
}

}  // namespace profiles
}  // namespace perftools
