// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "exception.h"

namespace pud {

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef float fp32;
typedef double fp64;

class ScopedFd {
 public:
  ScopedFd() : ScopedFd(-1) {}
  ScopedFd(int32 fd)  // NOLINT(runtime/explicit)
      : fd_(fd),
        autoclose_(fd >= 0) {}
  ScopedFd(const ScopedFd &that) = delete;
  ScopedFd(ScopedFd &&that)
      : fd_(that.fd_), autoclose_(that.autoclose_) {
    that.autoclose_ = false;
  }
  ~ScopedFd() { Close(); }

  ScopedFd &operator=(const ScopedFd &that) = delete;
  ScopedFd &operator=(ScopedFd &&that) {
    Close();
    fd_ = that.fd_;
    autoclose_ = that.autoclose_;
    that.autoclose_ = false;
    return *this;
  }

  operator int32() const { return fd_; }
  int32 fd() const { return fd_; }
  bool autoclose() const { return autoclose_; }
  void set_autoclose(bool val) { autoclose_ = val; }
  int32 release() {
    autoclose_ = false;
    return fd_;
  }

  void DupeFd(int32 to_fd) {
    if (fd_ == to_fd)
      return set_autoclose(false);
    if (dup2(fd_, to_fd) < 0)
      throw SystemError("Failed to duplicate fd");
  }

  void Close() {
    if (autoclose_)
      close(release());
  }

 private:
  int32 fd_;
  bool autoclose_;
};

class ScopedChildPid {
 public:
  ScopedChildPid(pid_t pid)  // NOLINT(runtime/explicit)
      : pid_(pid),
        wait_called_(false) {}
  ~ScopedChildPid();

  pid_t pid() const { return pid_; }

  pid_t Release() {
    wait_called_ = true;
    return pid_;
  }
  void Kill(int32 sig = 0);
  int Wait();
  void WaitCheckStatus();

 private:
  const pid_t pid_;
  bool wait_called_;
};

class File {
 public:
  typedef std::shared_ptr<File> Handle;

  virtual ~File() = default;
  virtual std::string Read() const = 0;
  virtual void Write(const std::string &buf) = 0;
};

File::Handle LocalFile(const std::string &path);

std::mt19937_64 &LocalRNG();

template <typename... Args>
std::string Concat(Args &&... args) {
  std::ostringstream ss;
  using List = int[];
  (void)List{0, ((void)(ss << args), 0)...};
  return ss.str();
}

std::string Format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

std::string ByteStringToString(const std::vector<uint8> &str);

std::vector<uint8> StringToByteString(const std::string &str);

std::string Base64Encode(const std::string &src);

std::string Base64Decode(const std::string &src);

}  // namespace pud
