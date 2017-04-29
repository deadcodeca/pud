// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <ostream>
#include <string>
#include <utility>
#include <vector>
#include "util.h"

namespace pud {

class BigInt {
 public:
  typedef uint32 LimbType;
  typedef uint64 DoubleLimbType;
  typedef int64 SignedDoubleLimbType;
  typedef std::vector<LimbType> BufferType;

  BigInt() {}
  BigInt(uint64 n);                            // NOLINT(runtime/explicit)
  BigInt(const std::string &str);              // NOLINT(runtime/explicit)
  BigInt(const std::vector<uint8> &byte_str);  // NOLINT(runtime/explicit)

  uint64 ToUInteger() const;
  std::string ToString() const;
  std::vector<uint8> ToByteString() const;

  BigInt &operator=(uint64 n);
  BigInt &operator+=(const BigInt &o);
  BigInt &operator-=(const BigInt &o);
  BigInt &operator*=(const BigInt &o);
  BigInt &operator/=(const BigInt &o);
  BigInt &operator%=(const BigInt &o);
  BigInt &operator&=(const BigInt &o);
  BigInt &operator|=(const BigInt &o);
  BigInt &operator<<=(size_t k);
  BigInt &operator>>=(size_t k);
  BigInt &operator++() { return operator+=(BigInt(1)); }
  BigInt &operator--() { return operator-=(BigInt(1)); }

  BigInt operator+(const BigInt &o) const { return BigInt(*this) += o; }
  BigInt operator-(const BigInt &o) const { return BigInt(*this) -= o; }
  BigInt operator*(const BigInt &o) const { return BigInt(*this) *= o; }
  BigInt operator/(const BigInt &o) const { return BigInt(*this) /= o; }
  BigInt operator%(const BigInt &o) const { return BigInt(*this) %= o; }
  BigInt operator&(const BigInt &o) const { return BigInt(*this) &= o; }
  BigInt operator|(const BigInt &o) const { return BigInt(*this) |= o; }
  BigInt operator<<(const size_t k) const { return BigInt(*this) <<= k; }
  BigInt operator>>(const size_t k) const { return BigInt(*this) >>= k; }

  bool operator<(const BigInt &o) const;
  bool operator<=(const BigInt &o) const;
  bool operator==(const BigInt &o) const;
  bool operator!=(const BigInt &o) const { return !operator==(o); }
  bool operator>=(const BigInt &o) const { return !operator<(o); }
  bool operator>(const BigInt &o) const { return !operator<=(o); }

  const BufferType &buffer() const { return buffer_; }

  static std::pair<BigInt, BigInt> ExpDivide(const BigInt &a, const BigInt &b);
  static size_t BitCount(const BigInt &n);
  static size_t LSB(const BigInt &n);
  static BigInt Random(size_t bitlen);
  static BigInt RandomPrime(size_t bitlen, bool verbose = false);
  static BigInt ModularPow(BigInt base, BigInt exponent, const BigInt &modulus);
  static BigInt ModularInverse(const BigInt &a, const BigInt &b);
  static BigInt GCD(BigInt a, BigInt b);

  friend std::ostream &operator<<(std::ostream &os, const BigInt &n) {
    return os << n.ToString();
  }

 private:
  explicit BigInt(BufferType buffer);

  BigInt &Normalize();
  static BufferType FromUInteger(uint64 n);

  BufferType buffer_;
};

class BigSInt {
 public:
  BigSInt() {}
  BigSInt(int64 n);          // NOLINT(runtime/explicit)
  BigSInt(const BigInt &n);  // NOLINT(runtime/explicit)

  BigSInt &operator+=(const BigSInt &o);
  BigSInt &operator-=(const BigSInt &o);
  BigSInt &operator*=(const BigSInt &o);
  BigSInt &operator/=(const BigSInt &o);

  BigSInt operator+(const BigSInt &o) const { return BigSInt(*this) += o; }
  BigSInt operator-(const BigSInt &o) const { return BigSInt(*this) -= o; }
  BigSInt operator*(const BigSInt &o) const { return BigSInt(*this) *= o; }
  BigSInt operator/(const BigSInt &o) const { return BigSInt(*this) /= o; }
  BigSInt operator-() const;

  bool operator<(const BigSInt &o) const;
  bool operator==(const BigSInt &o) const;
  bool operator!=(const BigSInt &o) const { return !operator==(o); }

  const BigInt &ToUnsigned() const { return num_; }

 private:
  BigSInt &Normalize();

  bool sign_;
  BigInt num_;
};

}  // namespace pud
