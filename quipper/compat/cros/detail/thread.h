// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_CROS_DETAIL_THREAD_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_CROS_DETAIL_THREAD_H_

#include "base/synchronization/waitable_event.h"
#include "base/threading/simple_thread.h"

namespace quipper {

class Thread : public quipper::compat::ThreadInterface,
               public base::DelegateSimpleThread::Delegate {
 public:
  explicit Thread(const string& name_prefix) : thread_(this, name_prefix) {}

  void Start() override { thread_.Start(); }

  void Join() override { thread_.Join(); }

  pid_t tid() override { return thread_.tid(); }

 protected:
  void Run() override = 0;

 private:
  base::DelegateSimpleThread thread_;
};

class Notification : public quipper::compat::NotificationInterface {
 public:
  Notification()
      : event_(true /* manual_reset */, false /* initially_signaled */) {}

  void Wait() override { event_.Wait(); }

  bool WaitWithTimeout(int timeout_ms) override {
    return event_.TimedWait(base::TimeDelta::FromMilliseconds(timeout_ms));
  }

  void Notify() override { event_.Signal(); }

 private:
  base::WaitableEvent event_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_CROS_DETAIL_THREAD_H_
