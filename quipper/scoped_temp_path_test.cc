// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_temp_path.h"

#include <string.h>  // for strlen.
#include <sys/stat.h>

#include <vector>

#include "base/logging.h"
#include "compat/string.h"
#include "compat/test.h"

namespace {

// For testing the creation of multiple temp paths.
const int kNumMultiplePaths = 32;

// When testing non-empty directories, populate them with this many files.
const int kNumFilesPerNonEmptyDirectory = 10;

// The length of the path template suffix used internally by ScopedTempPath and
// derived classes.
const size_t kTemplateSuffixSize = strlen("XXXXXX");

// Temporary paths use this prefix by default. Copied from scoped_temp_path.cc.
const char kTempPathTemplatePrefix[] = "/tmp/quipper.";

// Tests if |path| exists on the file system.
bool PathExists(const string& path) {
  struct stat buf;
  // stat() returns 0 on success, i.e. if the path exists and is valid.
  return !stat(path.c_str(), &buf);
}

// Creates some files in a directory. Returns the number of files created.
int PopulateDirectoryWithFiles(const string& dir, int num_files) {
  // The last six characters of the file template must be "XXXXXX".
  const char kPathTemplateSuffix[] = "/testXXXXXX";

  // The string providing the path template for creating temp files must not be
  // constant, so allocate some space here.
  size_t buf_size = dir.size() + strlen(kPathTemplateSuffix) + 1;
  char* path_template = new char[buf_size];

  int num_files_created = 0;
  for (int i = 0; i < num_files; ++i) {
    // Construct the mutable path template.
    snprintf(path_template, buf_size, "%s%s", dir.c_str(), kPathTemplateSuffix);
    // Create the file and make sure it is valid.
    int fd = mkstemp(path_template);
    if (fd == -1) {
      LOG(ERROR) << "Could not create file, errno=" << errno;
      continue;
    }
    ++num_files_created;
    close(fd);
  }
  delete[] path_template;

  return num_files_created;
}

}  // namespace

namespace quipper {

// Create one file and make sure it is deleted when out of scope.
TEST(ScopedTempPathTest, OneFile) {
  string path;
  {
    ScopedTempFile temp_file;
    path = temp_file.path();
    EXPECT_TRUE(PathExists(path)) << path;
    EXPECT_EQ(strlen(kTempPathTemplatePrefix) + kTemplateSuffixSize,
              path.size());
    EXPECT_EQ(kTempPathTemplatePrefix,
              path.substr(0, strlen(kTempPathTemplatePrefix)));
  }
  EXPECT_FALSE(PathExists(path)) << path;
}

// Create a file with a custom template filename.
TEST(ScopedTempPathTest, CustomFileTemplate) {
  string path;
  {
    const string prefix = "/tmp/foobar.";
    ScopedTempFile temp_file(prefix);
    path = temp_file.path();
    EXPECT_TRUE(PathExists(path)) << path;
    EXPECT_EQ(prefix.size() + kTemplateSuffixSize, path.size());
    EXPECT_EQ(prefix, path.substr(0, prefix.size()));
  }
  EXPECT_FALSE(PathExists(path)) << path;
}

// Create many files and make sure they are deleted when out of scope.
TEST(ScopedTempPathTest, MultipleFiles) {
  std::vector<string> paths(kNumMultiplePaths);
  {
    std::vector<ScopedTempFile> temp_files(kNumMultiplePaths);
    for (size_t i = 0; i < kNumMultiplePaths; ++i) {
      paths[i] = temp_files[i].path();
      EXPECT_TRUE(PathExists(paths[i])) << paths[i];
    }
  }
  for (size_t i = 0; i < kNumMultiplePaths; ++i) {
    EXPECT_FALSE(PathExists(paths[i])) << paths[i];
  }
}

// Create one empty directory and make sure it is deleted when out of scope.
TEST(ScopedTempPathTest, OneEmptyDir) {
  string path;
  {
    ScopedTempDir temp_path;
    path = temp_path.path();
    EXPECT_TRUE(PathExists(path)) << path;
    EXPECT_EQ('/', path.back()) << "Should append a slash";
    EXPECT_EQ(strlen(kTempPathTemplatePrefix) + kTemplateSuffixSize + 1,
              path.size());
    EXPECT_EQ(kTempPathTemplatePrefix,
              path.substr(0, strlen(kTempPathTemplatePrefix)));
  }
  EXPECT_FALSE(PathExists(path)) << path;
}

// Create a file with a custom template dirname.
TEST(ScopedTempPathTest, CustomDirTemplate) {
  string path;
  {
    const string prefix = "/tmp/foobar.";
    ScopedTempDir temp_path(prefix);
    path = temp_path.path();
    EXPECT_TRUE(PathExists(path)) << path;
    EXPECT_EQ('/', path.back()) << "Should append a slash";
    // Check prefix matches:
    EXPECT_EQ(prefix.size() + kTemplateSuffixSize + 1, path.size());
    EXPECT_EQ(prefix, path.substr(0, prefix.size()));
  }
  EXPECT_FALSE(PathExists(path)) << path;
}

// Create many empty directories and make sure they are deleted when out of
// scope.
TEST(ScopedTempPathTest, MultipleEmptyDirs) {
  std::vector<string> paths(kNumMultiplePaths);
  {
    std::vector<ScopedTempDir> temp_dirs(kNumMultiplePaths);
    for (size_t i = 0; i < kNumMultiplePaths; ++i) {
      paths[i] = temp_dirs[i].path();
      EXPECT_TRUE(PathExists(paths[i])) << paths[i];
    }
  }
  for (size_t i = 0; i < kNumMultiplePaths; ++i) {
    EXPECT_FALSE(PathExists(paths[i])) << paths[i];
  }
}

// Create a directory with some files in it, and make sure it is deleted when
// out of scope.
TEST(ScopedTempPathTest, OneNonEmptyDir) {
  string path;
  {
    ScopedTempDir temp_path;
    path = temp_path.path();
    EXPECT_TRUE(PathExists(path)) << path;
    // Populate the directory with files.
    EXPECT_EQ(kNumFilesPerNonEmptyDirectory,
              PopulateDirectoryWithFiles(path, kNumFilesPerNonEmptyDirectory));
  }
  EXPECT_FALSE(PathExists(path)) << path;
}

// Create many empty directories with files in them, and make sure they are
// deleted when out of scope.
TEST(ScopedTempPathTest, MultipleNonEmptyDirs) {
  std::vector<string> paths(kNumMultiplePaths);
  {
    std::vector<ScopedTempDir> temp_dirs(kNumMultiplePaths);
    for (size_t i = 0; i < kNumMultiplePaths; ++i) {
      paths[i] = temp_dirs[i].path();
      EXPECT_TRUE(PathExists(paths[i])) << paths[i];
      // Populate the directory with files.
      EXPECT_EQ(
          kNumFilesPerNonEmptyDirectory,
          PopulateDirectoryWithFiles(paths[i], kNumFilesPerNonEmptyDirectory));
    }
  }
  for (size_t i = 0; i < kNumMultiplePaths; ++i) {
    EXPECT_FALSE(PathExists(paths[i])) << paths[i];
  }
}

}  // namespace quipper
