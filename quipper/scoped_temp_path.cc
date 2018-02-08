// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_temp_path.h"

#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <vector>

#include "base/logging.h"

namespace {

// Temporary paths use this prefix by default.
const char kTempPathTemplatePrefix[] = "/tmp/quipper.";

// Maximum number of directories that nftw() will hold open simultaneously.
const int kNumOpenFds = 4;

// Callback for nftw(). Deletes each file it is given.
int FileDeletionCallback(const char* path, const struct stat* sb,
                         int /* type_flag */, struct FTW* /* ftwbuf */) {
  if (path && remove(path))
    LOG(ERROR) << "Could not remove " << path << ", errno=" << errno;
  return 0;
}

// Make a mutable copy (mkstemp modifies its argument), and append "XXXXXX".
// A vector<char> is used because string does not have an API for mutable
// direct access to the char data. That is, string::data() returns
// (const char *), and there is no non-const overload. (This appears to be an
// oversight of the standard since C++11.)
std::vector<char> MakeTempfileTemplate(string path_template) {
  path_template += "XXXXXX";
  path_template.push_back('\0');
  return std::vector<char>(path_template.begin(), path_template.end());
}

}  // namespace

namespace quipper {

ScopedTempFile::ScopedTempFile() : ScopedTempFile(kTempPathTemplatePrefix) {}

ScopedTempFile::ScopedTempFile(const string prefix) {
  std::vector<char> filename = MakeTempfileTemplate(prefix);
  int fd = mkstemp(filename.data());
  if (fd == -1) return;
  close(fd);
  path_ = string(filename.data());
}

ScopedTempDir::ScopedTempDir() : ScopedTempDir(kTempPathTemplatePrefix) {}

ScopedTempDir::ScopedTempDir(const string prefix) {
  std::vector<char> dirname = MakeTempfileTemplate(prefix);
  if (!mkdtemp(dirname.data())) return;
  path_ = string(dirname.data()) + "/";
}

ScopedTempPath::~ScopedTempPath() {
  // Recursively delete the path. Meaning of the flags:
  //   FTW_DEPTH: Handle directories after their contents.
  //   FTW_PHYS:  Do not follow symlinks.
  if (!path_.empty() && nftw(path_.c_str(), FileDeletionCallback, kNumOpenFds,
                             FTW_DEPTH | FTW_PHYS)) {
    LOG(ERROR) << "Error while using ftw() to remove " << path_;
  }
}

}  // namespace quipper
