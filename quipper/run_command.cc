// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_command.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "base/logging.h"

#include "compat/string.h"

namespace quipper {

namespace {

bool CloseFdOnExec(int fd) {
  int fd_flags = fcntl(fd, F_GETFD);
  if (fd_flags == -1) {
    PLOG(ERROR) << "F_GETFD";
    return false;
  }
  if (fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC)) {
    PLOG(ERROR) << "F_SETFD FD_CLOEXEC";
    return false;
  }
  return true;
}

void ReadFromFd(int fd, std::vector<char>* output) {
  static const int kReadSize = 4096;
  ssize_t read_sz;
  size_t read_off = output->size();
  do {
    output->resize(read_off + kReadSize);
    do {
      read_sz = read(fd, output->data() + read_off, kReadSize);
    } while (read_sz < 0 && errno == EINTR);
    if (read_sz < 0) {
      PLOG(FATAL) << "read";
      break;
    }
    read_off += read_sz;
  } while (read_sz > 0);
  output->resize(read_off);
}

}  // namespace

int RunCommand(const std::vector<string>& command, std::vector<char>* output) {
  std::vector<char*> c_str_cmd;
  c_str_cmd.reserve(command.size() + 1);
  for (const auto& c : command) {
    // This cast is safe: POSIX states that exec shall not modify argv nor the
    // strings pointed to by argv.
    c_str_cmd.push_back(const_cast<char*>(c.c_str()));
  }
  c_str_cmd.push_back(nullptr);

  // Create pipe for stdout:
  int output_pipefd[2];
  if (output) {
    if (pipe(output_pipefd)) {
      PLOG(ERROR) << "pipe";
      return -1;
    }
  }

  // Pipe for the child to return errno if exec fails:
  int errno_pipefd[2];
  if (pipe(errno_pipefd)) {
    PLOG(ERROR) << "pipe for errno";
    return -1;
  }
  if (!CloseFdOnExec(errno_pipefd[1])) return -1;

  const pid_t child = fork();
  if (child == 0) {
    close(errno_pipefd[0]);

    if (output) {
      if (close(output_pipefd[0]) < 0) {
        PLOG(FATAL) << "close read end of pipe";
      }
    }

    int devnull_fd = open("/dev/null", O_WRONLY);
    if (devnull_fd < 0) {
      PLOG(FATAL) << "open /dev/null";
    }

    if (dup2(output ? output_pipefd[1] : devnull_fd, 1) < 0) {
      PLOG(FATAL) << "dup2 stdout";
    }

    if (dup2(devnull_fd, 2) < 0) {
      PLOG(FATAL) << "dup2 stderr";
    }

    if (close(devnull_fd) < 0) {
      PLOG(FATAL) << "close /dev/null";
    }

    execvp(c_str_cmd[0], c_str_cmd.data());
    int exec_errno = errno;

    // exec failed... Write errno to a pipe so parent can retrieve it.
    int ret;
    do {
      ret = write(errno_pipefd[1], &exec_errno, sizeof(exec_errno));
    } while (ret < 0 && errno == EINTR);
    close(errno_pipefd[1]);

    std::_Exit(EXIT_FAILURE);
  }

  if (close(errno_pipefd[1])) {
    PLOG(FATAL) << "close write end of errno pipe";
  }
  if (output) {
    if (close(output_pipefd[1]) < 0) {
      PLOG(FATAL) << "close write end of pipe";
    }
  }

  // Check for errno:
  int child_exec_errno;
  int read_errno_res;
  do {
    read_errno_res =
        read(errno_pipefd[0], &child_exec_errno, sizeof(child_exec_errno));
  } while (read_errno_res < 0 && errno == EINTR);
  if (read_errno_res < 0) {
    PLOG(FATAL) << "read errno";
  }
  if (close(errno_pipefd[0])) {
    PLOG(FATAL) << "close errno";
  }

  if (read_errno_res > 0) {
    // exec failed in the child.
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
    }
    errno = child_exec_errno;
    return -1;
  }

  // Read stdout from pipe.
  if (output) {
    ReadFromFd(output_pipefd[0], output);
    if (close(output_pipefd[0])) {
      PLOG(FATAL) << "close output";
    }
  }

  // Wait for child.
  int exit_status;
  while (waitpid(child, &exit_status, 0) < 0 && errno == EINTR) {
  }
  errno = 0;
  if (WIFEXITED(exit_status)) return WEXITSTATUS(exit_status);
  return -1;
}

}  // namespace quipper
