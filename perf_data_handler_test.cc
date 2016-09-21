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

#include <vector>

#include "path_matching.h"
#include "string_compat.h"
#include "test_compat.h"

namespace perftools {
namespace {

TEST(PathMatching, DeletedSharedObjectMatching) {
  const std::vector<string> paths = {
      "lib.so.v1(deleted)",
      "lib.so.v1(deleted)junk",
      "lib.so (deleted)",
      "lib.so_junk_(deleted)",
      "lib.so   .so junk_(deleted)",
  };
  for (const auto& path : paths) {
    ASSERT_TRUE(perftools::IsDeletedSharedObject(path));
  }
}

TEST(PathMatching, DeletedSharedObjectNotMatching) {
  const std::vector<string> paths = {
      "abc",
      "lib.so ",
      "lib.so(deleted)",
      ".so (deleted)",
      "lib.sojunk(deleted)",
      "",
  };

  for (const auto& path : paths) {
    ASSERT_FALSE(perftools::IsDeletedSharedObject(path));
  }
}

TEST(PathMatching, VersionedSharedObjectMatching) {
  const std::vector<string> paths = {
      "lib.so.", "lib.so.abc", "lib.so.1", "lib.so.v1",
  };
  for (const auto& path : paths) {
    ASSERT_TRUE(perftools::IsVersionedSharedObject(path));
  }
}

TEST(PathMatching, VersionedSharedObjectNotMatching) {
  const std::vector<string> paths = {
      "abc", "lib.so(deleted)", ".so.v1", ".so.", "",
  };
  for (const auto& path : paths) {
    ASSERT_FALSE(perftools::IsDeletedSharedObject(path));
  }
}

}  // namespace
}  // namespace perftools

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
