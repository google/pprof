// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_MYBASE_BASE_LOGGING_H_
#define CHROMIUMOS_WIDE_PROFILING_MYBASE_BASE_LOGGING_H_

#include <errno.h>   // for errno
#include <string.h>  // for strerror

#include <iostream>  
#include <sstream>
#include <string>

#include "base/macros.h"

// Emulate Chrome-like logging.

// LogLevel is an enumeration that holds the log levels like libbase does.
enum LogLevel {
  INFO,
  WARNING,
  ERROR,
  FATAL,
};

namespace logging {

// Sets the log level. Anything at or above this level will be written to the
// log file/displayed to the user (if applicable). Anything below this level
// will be silently ignored. The log level defaults to 0 (everything is logged
// up to level INFO) if this function is not called.
// Note that log messages for VLOG(x) are logged at level -x, so setting
// the min log level to negative values enables verbose logging.
void SetMinLogLevel(int level);

// Gets the current log level.
int GetMinLogLevel();

// Gets the VLOG verbosity level.
int GetVlogVerbosity();

// Generic logging class that emulates logging from libbase. Do not use this
// class directly.
class LogBase {
 public:
  virtual ~LogBase() {}

  template <class T>
  LogBase& operator<<(const T& x) {
    ss_ << x;
    return *this;
  }

 protected:
  LogBase(const std::string label, const char* file, int line) {
    ss_ << "[" << label << ":" << file << ":" << line << "] ";
  }

  // Accumulates the contents to be printed.
  std::ostringstream ss_;
};

// For general logging.
class Log : public LogBase {
 public:
  Log(LogLevel level, const char* level_str, const char* file, int line)
      : LogBase(level_str, file, line) {
    level_ = level;
  }

  ~Log() {
    if (level_ >= GetMinLogLevel()) std::cerr << ss_.str() << std::endl;
    if (level_ >= FATAL) exit(EXIT_FAILURE);
  }

 protected:
  LogLevel level_;
};

// Like LOG but appends errno's string description to the logging.
class PLog : public Log {
 public:
  PLog(LogLevel level, const char* level_str, const char* file, int line)
      : Log(level, level_str, file, line) {}

  ~PLog() {
    if (level_ >= GetMinLogLevel())
      std::cerr << ss_.str() << ": " << strerror(errnum_) << std::endl;
  }

 private:
  // Cached error value, in case errno changes during this object's lifetime.
  int errnum_;
};

// Like LOG but conditional upon the logging verbosity level.
class VLog : public LogBase {
 public:
  VLog(int vlog_level, const char* file, int line)
      : LogBase(std::string("VLOG(") + std::to_string(vlog_level) + ")", file,
                line),
        vlog_level_(vlog_level) {}

  ~VLog() {
    if (vlog_level_ <= GetVlogVerbosity()) std::cerr << ss_.str() << std::endl;
  }

 private:
  // Logging verbosity level. The logging will be printed if this value is less
  // than or equal to GetVlogVerbosity().
  int vlog_level_;
};

}  // namespace logging

// These macros are for LOG() and related logging commands.
#define LOG(level) logging::Log(level, #level, __FILE__, __LINE__)
#define PLOG(level) logging::PLog(level, #level, __FILE__, __LINE__)
#define VLOG(level) logging::VLog(level, __FILE__, __LINE__)

// Some macros from libbase that we use.
#define CHECK(x) \
  if (!(x)) LOG(FATAL) << #x
#define CHECK_GT(x, y) \
  if (!(x > y)) LOG(FATAL) << #x << " > " << #y << "failed"
#define CHECK_GE(x, y) \
  if (!(x >= y)) LOG(FATAL) << #x << " >= " << #y << "failed"
#define CHECK_LE(x, y) \
  if (!(x <= y)) LOG(FATAL) << #x << " <= " << #y << "failed"
#define CHECK_NE(x, y) \
  if (!(x != y)) LOG(FATAL) << #x << " != " << #y << "failed"
#define CHECK_EQ(x, y) \
  if (!(x == y)) LOG(FATAL) << #x << " == " << #y << "failed"
#define DLOG(x) LOG(x)
#define DVLOG(x) VLOG(x)
#define DCHECK(x) CHECK(x)

#endif  // CHROMIUMOS_WIDE_PROFILING_MYBASE_BASE_LOGGING_H_
