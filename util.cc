// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <algorithm>
#include <fstream>
#include <string>
#include "exception.h"

namespace pud {
namespace {

const char kBase64EncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const char kBase64DecodeTable[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  62, 0,  0,  0,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,
    0,  0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0};

class FileImpl : public File {
 public:
  explicit FileImpl(const std::string &path) : path_(path) {}
  ~FileImpl() override {}

  std::string Read() const override;
  void Write(const std::string &buf) override;

 private:
  const std::string path_;
};

template <typename URND>
class SeedSequenceGenerator {
 public:
  typedef typename URND::result_type result_type;

  template <typename It>
  void generate(It start, It end) {
    for (; start != end; ++start)
      *start = rand_();
  }

 private:
  URND rand_;
};

}  // anonymous namespace

ScopedChildPid::~ScopedChildPid() {
  if (pid_ > 0 && !wait_called_) {
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
  }
}

void ScopedChildPid::Kill(int32 sig) {
  if (pid_ <= 0)
    throw InvalidArgument("Invalid pid passed to kill()");
  kill(pid_, sig != 0 ? sig : SIGKILL);
}

int ScopedChildPid::Wait() {
  wait_called_ = true;
  if (pid_ <= 0)
    throw InvalidArgument("Invalid pid passed to waitpid()");
  int status;
  const int32 ret = waitpid(pid_, &status, 0);
  if (ret < 0)
    throw SystemError("Failed to call waitpid on child process");
  return status;
}

void ScopedChildPid::WaitCheckStatus() {
  const int status = Wait();
  if (WIFEXITED(status)) {
    const int32 code = WEXITSTATUS(status);
    if (code != 0)
      throw UnknownError(
          Concat("Child process exited with status code ", code));
  } else if (WIFSIGNALED(status)) {
    throw UnknownError(
        Concat("Child process killed by signal ", WTERMSIG(status)));
  } else {
    throw UnknownError("Child process exited with unknown error");
  }
}

std::string FileImpl::Read() const {
  std::ifstream file(path_, std::ifstream::in | std::ifstream::binary);
  if (file.fail())
    throw SystemError(Concat("Error opening file ", path_));
  std::ostringstream stream;
  stream << file.rdbuf();
  file.close();
  if (file.bad())
    throw SystemError(Concat("Error reading file ", path_));
  return stream.str();
}

void FileImpl::Write(const std::string &buf) {
  std::ofstream file(path_, std::ifstream::out | std::ifstream::binary);
  if (file.fail())
    throw SystemError(Concat("Error opening file ", path_));
  file << buf;
  file.close();
  if (file.bad())
    throw SystemError(Concat("Error writing file ", path_));
}

File::Handle LocalFile(const std::string &path) {
  return std::make_shared<FileImpl>(path);
}

std::mt19937_64 &LocalRNG() {
  thread_local SeedSequenceGenerator<std::random_device> seed;
  thread_local std::mt19937_64 engine{seed};
  return engine;
}

std::string Format(const char *fmt, ...) {
  va_list arg_list;
  va_start(arg_list, fmt);

  char *buf = nullptr;
  assert(vasprintf(&buf, fmt, arg_list) >= 0);
  va_end(arg_list);
  std::unique_ptr<char, void (*)(void *)> buf_ptr(buf, free);
  return std::string(buf);
}

std::string ByteStringToString(const std::vector<uint8> &str) {
  return std::string(str.begin(), str.end());
}

std::vector<uint8> StringToByteString(const std::string &str) {
  return std::vector<uint8>(str.data(), str.data() + str.size());
}

std::string Base64Encode(const std::string &src) {
  const uint8 *ptr = reinterpret_cast<const uint8 *>(src.data());
  const uint8 *end = ptr + src.size();
  std::string out;
  out.reserve(((src.size() + 2) / 3) * 4);
  for (; ptr < end; ptr += 3) {
    std::ptrdiff_t len = end - ptr;
    out.push_back(kBase64EncodeTable[ptr[0] >> 2]);
    out.push_back(kBase64EncodeTable[((ptr[0] & 0x03) << 4) |
                                     (len > 1 ? ((ptr[1] & 0xf0) >> 4) : 0)]);
    if (len > 1) {
      out.push_back(kBase64EncodeTable[((ptr[1] & 0x0f) << 2) |
                                       (len > 2 ? ((ptr[2] & 0xc0) >> 6) : 0)]);
    }
    if (len > 2) {
      out.push_back(kBase64EncodeTable[ptr[2] & 0x3f]);
    }
  }
  while ((out.size() % 4) != 0)
    out.push_back('=');
  return out;
}

std::string Base64Decode(const std::string &src) {
  const uint8 *ptr = reinterpret_cast<const uint8 *>(src.data());
  const uint8 *end = ptr + src.size();
  while (end != ptr && end[-1] == '=')
    --end;
  std::string out;
  out.reserve(((src.size() + 3) / 4) * 3);
  for (; ptr < end; ptr += 4) {
    std::ptrdiff_t len = end - ptr;
    if (len > 1) {
      out.push_back(static_cast<char>((kBase64DecodeTable[ptr[0]] << 2) |
                                      (kBase64DecodeTable[ptr[1]] >> 4)));
    }
    if (len > 2) {
      out.push_back(
          static_cast<char>(((kBase64DecodeTable[ptr[1]] & 0x0f) << 4) |
                            (kBase64DecodeTable[ptr[2]] >> 2)));
    }
    if (len > 3) {
      out.push_back(
          static_cast<char>(((kBase64DecodeTable[ptr[2]] & 0x03) << 6) |
                            (kBase64DecodeTable[ptr[3]])));
    }
  }
  return out;
}

}  // namespace pud
