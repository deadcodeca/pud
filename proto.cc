// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "proto.h"

#include <assert.h>
#include <string>
#include "exception.h"
#include "util.h"

namespace pud {

void OutputBuffer::Reset() {
  data_.clear();
}

void OutputBuffer::Push(const std::vector<uint8> &data) {
  Push(data.cbegin(), data.cend());
}

void OutputBuffer::Push(std::vector<uint8>::const_iterator begin,
                        std::vector<uint8>::const_iterator end) {
  data_.insert(data_.end(), begin, end);
}

void OutputBuffer::PushU8(uint8 n) {
  data_.emplace_back(n);
}

void OutputBuffer::PushU16(uint16 n) {
  PushU8((n >> 8) & 0xff);
  PushU8(n & 0xff);
}

void OutputBuffer::PushU32(uint32 n) {
  PushU16((n >> 16) & 0xffff);
  PushU16(n & 0xffff);
}

void OutputBuffer::PushU64(uint64 n) {
  PushU32((n >> 32) & 0xffffffff);
  PushU32(n & 0xffffffff);
}

void OutputBuffer::PushVariableLength(size_t length) {
  while (length >= 0x7f) {
    PushU8(0x80 | (length & 0x7f));
    length >>= 7;
  }
  PushU8(length & 0x7f);
}

std::string OutputBuffer::ToString() const {
  return ByteStringToString(data_);
}

InputBuffer::InputBuffer() : InputBuffer(std::vector<uint8>{}) {
}

InputBuffer::InputBuffer(const std::string &buf)
    : InputBuffer(StringToByteString(buf)) {
}

InputBuffer::InputBuffer(const std::vector<uint8> &buf)
    : data_(buf), ptr_(data_.cbegin()) {
}

void InputBuffer::Reset(const std::string &buf) {
  Reset(StringToByteString(buf));
}

void InputBuffer::Reset(const std::vector<uint8> &buf) {
  data_ = buf;
  ptr_ = data_.cbegin();
}

std::vector<uint8> InputBuffer::Pop(size_t length) {
  std::vector<uint8> data;
  for (size_t i = 0; i < length; i++)
    data.emplace_back(PopU8());
  return data;
}

uint8 InputBuffer::PopU8() {
  if (ptr_ == data_.end())
    throw OutOfRange("Unexpected end of request data");
  return *ptr_++;
}

uint16 InputBuffer::PopU16() {
  return ((static_cast<uint16>(PopU8()) << 8) | PopU8()) & 0xffff;
}

uint32 InputBuffer::PopU32() {
  return ((static_cast<uint32>(PopU16()) << 16) | PopU16()) & 0xffffffff;
}

uint64 InputBuffer::PopU64() {
  return (static_cast<uint64>(PopU32()) << 32) | PopU32();
}

size_t InputBuffer::PopVariableLength() {
  size_t length = 0;
  size_t shift = 0;
  for (;;) {
    const uint8 n = PopU8();
    length |= static_cast<size_t>(n & 0x7f) << shift;
    if (n < 0x7f)
      break;
    shift += 7;
  }
  return length;
}

}  // namespace pud
