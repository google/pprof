// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_SCOPED_TEMP_PATH_H_
#define CHROMIUMOS_WIDE_PROFILING_SCOPED_TEMP_PATH_H_

#include <string>

#include "base/macros.h"

#include "compat/string.h"

namespace quipper {

// Used to create a temporary file or directory.
class ScopedTempPath {
 public:
  ScopedTempPath() {}
  // The temporary path will be removed when the object is destroyed.
  virtual ~ScopedTempPath();
  const string& path() const { return path_; }

 protected:
  string path_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedTempPath);
};

class ScopedTempFile : public ScopedTempPath {
 public:
  // Create a temporary file.  If successful, the path will be stored in
  // |path_|.  If not, |path_| will be an empty string.
  ScopedTempFile();
  // A temporary file is created using mkstemp() by adding "XXXXXX" to
  // |prefix|.
  explicit ScopedTempFile(string prefix);
};

class ScopedTempDir : public ScopedTempPath {
 public:
  // Create a temporary directory.  If successful, the path will be stored in
  // |path_|.  If not, |path_| will be an empty string.
  ScopedTempDir();
  // A temporary file is created using mkdtemp() by adding "XXXXXX" to
  // |prefix|.
  explicit ScopedTempDir(string prefix);
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_SCOPED_TEMP_PATH_H_
