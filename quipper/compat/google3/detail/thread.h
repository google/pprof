#ifndef QUIPPER_COMPAT_GOOGLE3_DETAIL_THREAD_H_
#define QUIPPER_COMPAT_GOOGLE3_DETAIL_THREAD_H_

#include <sys/types.h>

#include "base/notification.h"
#include "base/sysinfo.h"
#include "thread/thread.h"
#include "thread/thread_options.h"

namespace quipper {

class Thread : public quipper::compat::ThreadInterface {
 public:
  explicit Thread(const string& name_prefix)
      : thread_(thread::Options().set_joinable(true), name_prefix,
                this, &Thread::ThreadMain) {
  }

  void Start() override {
    thread_.Start();
    started_.WaitForNotification();
  }

  void Join() override {
    thread_.Join();
  }

  pid_t tid() override {
    return tid_;
  }

 protected:
  void Run() override = 0;

 private:
  void ThreadMain() {
    tid_ = GetTID();
    started_.Notify();
    Run();
  }

  pid_t tid_;
  ::Notification started_;
  MemberThread<Thread> thread_;
};

class Notification : public quipper::compat::NotificationInterface {
 public:
  Notification() {}

  void Wait() override {
    notification_.WaitForNotification();
  }

  bool WaitWithTimeout(int timeout_ms) override {
    notification_.WaitForNotificationWithTimeout(
        base::Milliseconds(timeout_ms));
  }

  void Notify() override {
    notification_.Notify();
  }

 private:
  ::Notification notification_;
};

}  // namespace quipper

#endif  // QUIPPER_COMPAT_GOOGLE3_DETAIL_THREAD_H_
