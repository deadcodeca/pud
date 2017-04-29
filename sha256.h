// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <cstddef>
#include <vector>
#include "util.h"

namespace pud {

class SHA256 {
 public:
  SHA256();

  void Update(const std::vector<uint8> &data) {
    Update(data.begin(), data.end());
  }

  void Update(std::vector<uint8>::const_iterator begin,
              std::vector<uint8>::const_iterator end);

  void Final();

  std::vector<uint8> hash() const {
    return std::vector<uint8>(hash_, hash_ + sizeof(hash_));
  }

 private:
  void Transform(const uint8 *buf);

  size_t curlen_;
  size_t length_;
  uint8 buf_[64];
  uint32 state_[8];
  uint8 hash_[32];
};

SHA256 Hash(std::vector<uint8>::const_iterator begin,
            std::vector<uint8>::const_iterator end);

inline SHA256 Hash(const std::vector<uint8> &data) {
  return Hash(data.begin(), data.end());
}

}  // namespace pud
