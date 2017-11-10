// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_EXT_DETAIL_THREAD_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_EXT_DETAIL_THREAD_H_
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace quipper {
class Thread : public quipper::compat::ThreadInterface {
 public:
  explicit Thread(const string& name_prefix) {}

  void Start() override { thread_ = std::thread(&Thread::Run, this); }

  void Join() override { thread_.join(); }

  pid_t tid() override { return thread_.native_handle(); }

 protected:
  void Run() override = 0;

 private:
  std::thread thread_;
};

class Notification : public quipper::compat::NotificationInterface {
 public:
  void Wait() override {
    std::unique_lock<std::mutex> lock(mutex_);
    event_.wait(lock);
  }

  bool WaitWithTimeout(int timeout_ms) override {
    std::unique_lock<std::mutex> lock(mutex_);
    return event_.wait_for(lock, std::chrono::milliseconds(timeout_ms)) ==
           std::cv_status::no_timeout;
  }

  void Notify() override { event_.notify_all(); }

 private:
  std::condition_variable event_;
  std::mutex mutex_;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_EXT_DETAIL_THREAD_H_
