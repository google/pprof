/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Google Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Tests converting perf.data files to sets of Profile

#include "perf_data_converter.h"

#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "chromiumos-wide-profiling/perf_parser.h"
#include "chromiumos-wide-profiling/perf_reader.h"
#include "int_compat.h"
#include "intervalmap.h"
#include "profile_wrappers.pb.h"
#include "string_compat.h"
#include "test_compat.h"

using quipper::PerfDataProto;
using perftools::profiles::Profile;
using perftools::profiles::Location;
using perftools::profiles::Mapping;

namespace {

using ProfileVector = std::vector<std::unique_ptr<Profile>>;

// GetMapCounts returns a map keyed by a location identifier and
// mapping to self and total counts for that location.
std::unordered_map<string, std::pair<int64, int64>> GetMapCounts(
    const std::vector<std::unique_ptr<Profile>>& profiles) {
  std::unordered_map<string, std::pair<int64, int64>> map_counts;
  for (const auto& profile : profiles) {
    std::unordered_map<uint64, const Location*> locations;
    perftools::IntervalMap<const Mapping*> mappings;
    if (profile->mapping_size() <= 0) {
      std::cerr << "Invalid mapping size: " << profile->mapping_size()
                << std::endl;
      abort();
    }
    const Mapping& main = profile->mapping(0);
    for (const auto& mapping : profile->mapping()) {
      mappings.Set(mapping.memory_start(), mapping.memory_limit(), &mapping);
    }
    for (const auto& location : profile->location()) {
      locations[location.id()] = &location;
    }
    for (int i = 0; i < profile->sample_size(); ++i) {
      const auto& sample = profile->sample(i);
      for (int id_index = 0; id_index < sample.location_id_size(); ++id_index) {
        uint64 id = sample.location_id(id_index);
        if (!locations[id]) {
          std::cerr << "No location for id: " << id << std::endl;
          abort();
        }

        std::stringstream key_stream;
        key_stream << profile->string_table(main.filename()) << ":"
                   << profile->string_table(main.build_id());
        if (locations[id]->mapping_id() != 0) {
          const Mapping* dso;
          uint64 addr = locations[id]->address();
          if (!mappings.Lookup(addr, &dso)) {
            std::cerr << "no mapping for id: " << std::hex << addr << std::endl;
            abort();
          }
          key_stream << "+" << profile->string_table(dso->filename()) << ":"
                     << profile->string_table(dso->build_id()) << std::hex
                     << (addr - dso->memory_start());
        }
        const auto& key = key_stream.str();
        auto count = map_counts[key];
        if (id_index == 0) {
          // Exclusive.
          ++count.first;
        } else {
          // Inclusive.
          ++count.second;
        }
        map_counts[key] = count;
      }
    }
  }
  return map_counts;
}

void PrintMapCounts(
    const std::unordered_map<string, std::pair<int64, int64>>& map_counts) {
  for (const auto& it : map_counts) {
    std::cout << it.first << " " << it.second.first << "+" << it.second.second;
  }
}

}  // namespace

namespace perftools {

// Reads the content of the file at path into a string. Aborts if it is unable
// to.
void GetContents(const string& path, string* content) {
  std::ifstream file(path);
  if ((file.rdstate() & std::ifstream::failbit) != 0) {
    std::cerr << "Failed to read contents of: " << path << std::endl;
    abort();
  }
  std::stringstream contents;
  contents << file.rdbuf();
  *content = contents.str();
}

// Set dir to the current directory, or return false if an error occurs.
bool GetCurrentDirectory(string* dir) {
  std::unique_ptr<char, decltype(std::free)*> cwd(getcwd(nullptr, 0),
                                                  std::free);
  if (cwd == nullptr) {
    return false;
  }
  *dir = cwd.get();
  return true;
}

// Gets the string after the last '/' or returns the entire string if there are
// no slashes.
inline string Basename(const string& path) {
  return path.substr(path.find_last_of("/"));
}

// Assumes relpath does not begin with a '/'
string GetResource(const string& relpath) {
  string cwd;
  GetCurrentDirectory(&cwd);
  return cwd + "/" + relpath;
}

string GetSerializedPerfDataProto(const string& raw_perf_data) {
  std::unique_ptr<quipper::PerfReader> reader(new quipper::PerfReader);
  EXPECT_TRUE(reader->ReadFromString(raw_perf_data));

  std::unique_ptr<quipper::PerfParser> parser;
  parser.reset(new quipper::PerfParser(reader.get()));
  EXPECT_TRUE(parser->ParseRawEvents());

  quipper::PerfDataProto perf_data;
  EXPECT_TRUE(reader->Serialize(&perf_data));

  string perf_data_str;
  perf_data.SerializeToString(&perf_data_str);
  return perf_data_str;
}

class PerfDataConverterTest : public ::testing::Test {
 protected:
  PerfDataConverterTest() {}
};

struct TestCase {
  string filename;
  int64 key_count;
  int64 total_exclusive;
  int64 total_inclusive;
};

// Builds a set of counts for each sample in the profile.  This is a
// very high-level test -- major changes in the values should
// be validated via manual inspection of new golden values.
TEST_F(PerfDataConverterTest, Converts) {
  string single_profile(
      GetResource("testdata/single-event-single-process.perf.data"));
  string multi_pid_profile(
      GetResource("testdata/single-event-multi-process.perf.data"));
  string multi_event_profile(
      GetResource("testdata/multi-event-single-process.perf.data"));
  string stack_profile(GetResource("testdata/with-callchain.perf.data"));

  std::vector<TestCase> cases;
  cases.emplace_back(TestCase{single_profile, 1061, 1061, 0});
  cases.emplace_back(TestCase{multi_pid_profile, 442, 730, 0});
  cases.emplace_back(TestCase{multi_event_profile, 1124, 1124, 0});
  cases.emplace_back(TestCase{stack_profile, 1138, 1210, 2247});

  for (const auto& c : cases) {
    std::stringstream casename;
    casename << "case " << Basename(c.filename);
    string raw_perf_data;
    GetContents(c.filename, &raw_perf_data);

    // Test RawPerfData input.
    ProfileVector profiles = RawPerfDataToProfileProto(
        reinterpret_cast<const void*>(raw_perf_data.c_str()),
        raw_perf_data.size(), nullptr, 0, kNoLabels, false);
    // Does not group by PID, Vector should only contain one element
    EXPECT_EQ(profiles.size(), 1);
    auto counts = GetMapCounts(profiles);
    EXPECT_EQ(c.key_count, counts.size()) << casename;
    int64 total_exclusive = 0;
    int64 total_inclusive = 0;
    for (const auto& it : counts) {
      total_exclusive += it.second.first;
      total_inclusive += it.second.second;
    }
    EXPECT_EQ(c.total_exclusive, total_exclusive) << casename;
    EXPECT_EQ(c.total_inclusive, total_inclusive) << casename;

    // Test SerializedPerfDataProto input.
    string perf_data_str = GetSerializedPerfDataProto(raw_perf_data);

    profiles = SerializedPerfDataProtoToProfileProto(
        perf_data_str, kNoLabels, false);
    counts = GetMapCounts(profiles);
    EXPECT_EQ(c.key_count, counts.size()) << casename;
    total_exclusive = 0;
    total_inclusive = 0;
    for (const auto& it : counts) {
      total_exclusive += it.second.first;
      total_inclusive += it.second.second;
    }
    EXPECT_EQ(c.total_exclusive, total_exclusive) << casename;
    EXPECT_EQ(c.total_inclusive, total_inclusive) << casename;
  }
}

TEST_F(PerfDataConverterTest, ConvertsGroupPid) {
  string multiple_profile(
      GetResource("testdata/single-event-multi-process.perf.data"));

  // Fetch the stdout_injected result and emit it to a profile.proto.  Group by
  // PIDs so the inner vector will have multiple entries.
  string raw_perf_data;
  GetContents(multiple_profile, &raw_perf_data);
  // Test SerializedPerfDataProto input.
  string perf_data_str = GetSerializedPerfDataProto(raw_perf_data);
  ProfileVector profiles = SerializedPerfDataProtoToProfileProto(
      perf_data_str, perftools::kPidAndTidLabels, true);

  uint64 total_samples = 0;
  // Samples were collected for 6 pids in this case, so the outer vector should
  // contain 6 profiles, one for each pid.
  int pids = 6;
  EXPECT_EQ(pids, profiles.size());
  for (const auto& per_thread : profiles) {
    for (const auto& sample : per_thread->sample()) {
      // Count only samples, which are the even numbers.  Total event counts
      // are the odds.
      for (int x = 0; x < sample.value_size(); x += 2) {
        total_samples += sample.value(x);
      }
    }
  }
  // The perf.data file contained 19989 original samples. Still should.
  EXPECT_EQ(19989, total_samples);
}

TEST_F(PerfDataConverterTest, Injects) {
  string path = GetResource("testdata/with-callchain.perf.data");
  string raw_perf_data;
  GetContents(path, &raw_perf_data);
  const string want_buildid = "abcdabcd";
  perftools::StringMap string_map;
  string_map.add_key_value();
  string_map.mutable_key_value(0)->set_key("[kernel.kallsyms]");
  string_map.mutable_key_value(0)->set_value(want_buildid);
  string serialized_string_map;
  if (!string_map.SerializeToString(&serialized_string_map)) {
    std::cerr << "Failed to serizlied string_map to string." << std::endl;
    abort();
  }

  // Test RawPerfData input.
  ProfileVector profiles = RawPerfDataToProfileProto(
      reinterpret_cast<const void*>(raw_perf_data.c_str()),
      raw_perf_data.size(),
      reinterpret_cast<const void*>(serialized_string_map.c_str()),
      serialized_string_map.size(), kNoLabels, false);
  bool found = false;
  for (const auto& profile : profiles) {
    for (const auto& it : profile->mapping()) {
      if (profile->string_table(it.build_id()) == want_buildid) {
        found = true;
        goto done;
      }
    }
  }
done:
  EXPECT_TRUE(found) << want_buildid << " not found in profiles";
}

}  // namespace perftools

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
