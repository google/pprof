// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_THREAD_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_THREAD_H_

namespace quipper {
namespace compat {

// The Thread implementation used by quipper implements this interface.
class ThreadInterface {
 public:
  // Constructor signature should match this:
  // Thread(const string& name_prefix);

  virtual ~ThreadInterface() {}

  // Start the thread.
  virtual void Start() = 0;
  // quipper threads must be joined exactly once.
  virtual void Join() = 0;

  virtual pid_t tid() = 0;

 protected:
  // Thread body. Override this.
  virtual void Run() = 0;
};

// Synchronization tool that blocks waiting threads until notified to proceed by
// another.
class NotificationInterface {
 public:
  // Constructor signature should match this:
  // Notification();

  virtual ~NotificationInterface() {}

  // Wait for Notify() to be called.
  virtual void Wait() = 0;
  // Wait for up to |timeout_ms| for a notification. Returns true if the
  // event was signaled. Returning false does not necessarily mean |timout_ms|
  // has passed.
  virtual bool WaitWithTimeout(int timeout_ms) = 0;

  // Trigger the notification, and wake up any waiting threads.
  virtual void Notify() = 0;
};

}  // namespace compat
}  // namespace quipper

#include "detail/thread.h"

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_THREAD_H_
