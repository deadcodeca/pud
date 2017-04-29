// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <string>
#include <vector>
#include "util.h"

namespace pud {

class OutputBuffer {
 public:
  void Reset();
  void Push(const std::vector<uint8> &data);
  void Push(std::vector<uint8>::const_iterator begin,
            std::vector<uint8>::const_iterator end);
  void PushU8(uint8 n);
  void PushU16(uint16 n);
  void PushU32(uint32 n);
  void PushU64(uint64 n);
  void PushVariableLength(size_t length);

  const std::vector<uint8> &data() const { return data_; }
  size_t size() const { return data_.size(); }

  std::string ToString() const;

 private:
  std::vector<uint8> data_;
};

class InputBuffer {
 public:
  InputBuffer();
  explicit InputBuffer(const std::string &buf);
  explicit InputBuffer(const std::vector<uint8> &buf);

  void Reset(const std::string &buf);
  void Reset(const std::vector<uint8> &buf);
  std::vector<uint8> Pop(size_t length);
  uint8 PopU8();
  uint16 PopU16();
  uint32 PopU32();
  uint64 PopU64();
  size_t PopVariableLength();

  std::vector<uint8>::const_iterator ptr() const { return ptr_; }
  bool empty() const { return ptr_ == data_.end(); }

 private:
  std::vector<uint8> data_;
  std::vector<uint8>::const_iterator ptr_;
};

}  // namespace pud
