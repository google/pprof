/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PROFILES_PROTO_BUILDER_H_
#define PERFTOOLS_PROFILES_PROTO_BUILDER_H_

#include <stddef.h>
#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
namespace perftools {
namespace profiles {

typedef int64_t int64;
typedef uint64_t uint64;
typedef std::string string;

}
}
#include "profile.pb.h"

namespace perftools {
namespace profiles {

// Provides mechanisms to facilitate the generation of profiles
// on a compressed protobuf:
// - Manages the creation of the string table.
// - Manages the creation of Functions for symbolized profiles.
// - Creates the association between locations and mappings.
// The caller should populate the profile with samples and their
// corresponding sample types, and any other optional fields.
class Builder {
 public:
  Builder();

  // Adds a string to the profile string table if not already present.
  // Returns a unique integer id for this string.
  int64 StringId(const char *str);

  // Adds a function with these attributes to the profile function
  // table, if not already present. Returns a unique integer id for
  // this function.
  uint64 FunctionId(const char *name, const char *system_name,
                    const char *file, int64 start_line);

  // Adds mappings for the currently running binary to the profile.
  void AddCurrentMappings();

  // Prepares the profile for encoding. Returns true on success.
  // If the profile has no locations, inserts location using the
  // location_ids from the samples as addresses.
  // Associates the locations to mappings by comparing the location
  // address into the mapping address range.
  bool Finalize();

  // Serializes and compresses the profile into a string, replacing
  // its contents. It calls Finalize() and returns whether the
  // encoding was successful.
  bool Emit(string *output);

  // Serializes and compresses a profile into a string, replacing its
  // contents. Returns false if there were errors on the serialization
  // or compression, and the output string will not contain valid data.
  static bool Marshal(const Profile &profile, string *output);

  // Serializes and compresses a profile into a file represented by a
  // file descriptor. Returns false if there were errors on the
  // serialization or compression.
  static bool MarshalToFile(const Profile &profile, int fd);

  // Serializes and compresses a profile into a file, creating a new
  // file or replacing its contents if it already exists.
  static bool MarshalToFile(const Profile &profile, const char *filename);

  // Determines if the profile is internally consistent (suitable for
  // serialization). Returns true if no errors were encountered.
  static bool CheckValid(const Profile &profile);

  // Extract the profile from the builder object. No further calls
  // should be made to the builder after this.
  std::unique_ptr<Profile> Consume() { return std::move(profile_); }

  // Returns the underlying profile, to populate any fields not
  // managed by the builder. The fields function and string_table
  // should be populated through Builder::StringId and
  // Builder::FunctionId.
  Profile *mutable_profile() { return profile_.get(); }

 private:
  // Holds the information about a function to facilitate deduplication.
  typedef std::tuple<int64, int64, int64, int64> Function;
  class FunctionHasher {
   public:
    size_t operator()(const Function &f) const {
      int64 hash = std::get<0>(f);
      hash = hash + ((hash << 8) ^ std::get<1>(f));
      hash = hash + ((hash << 8) ^ std::get<2>(f));
      hash = hash + ((hash << 8) ^ std::get<3>(f));
      return static_cast<size_t>(hash);
    }
  };

  // Hashes to deduplicate strings and functions.
  std::unordered_map<string, int64> strings_;
  std::unordered_map<Function, int64, FunctionHasher> functions_;

  // Actual profile being updated.
  std::unique_ptr<Profile> profile_;
};

}  // namespace profiles
}  // namespace perftools

#endif  // PERFTOOLS_PROFILES_PROTO_BUILDER_H_
