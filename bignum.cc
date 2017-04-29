// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "bignum.h"

#include <algorithm>
#include <bitset>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "util.h"

namespace std {
template <>
struct is_integral<pud::BigInt> : public true_type {};
}  // namespace std

namespace pud {
namespace {
const int64 kBaseBits = sizeof(BigInt::LimbType) * 8;
const BigInt::SignedDoubleLimbType kBase = BigInt::SignedDoubleLimbType(1)
                                           << kBaseBits;
const size_t kMillerRabinTrials = 12;

const BigInt kZero(0);
const BigInt kOne(1);
const BigInt kTen(10);

const BigSInt kSZero(0);
const BigSInt kSOne(1);

std::once_flag prime_list_once;
std::vector<uint64> prime_list;

template <size_t N>
std::vector<uint64> SieveOfSundaram() {
  std::bitset<N> sieve;
  sieve.flip();
  for (size_t i = 1; i < N; ++i) {
    const size_t low = (N - i) / (2 * i + 1);
    for (size_t j = i; j <= low; ++j)
      sieve[i + j + 2 * i * j] = false;
  }
  std::vector<uint64> primes;
  primes.push_back(2);
  for (size_t i = 1; i < sieve.size(); ++i) {
    if (sieve[i])
      primes.push_back(i * 2 + 1);
  }
  return primes;
}

void InitPrimeList() {
  prime_list = SieveOfSundaram<8932>();
}

}  // anonymous namespace

BigInt::BigInt(uint64 n) : BigInt(FromUInteger(n)) {
}

BigInt::BigInt(const std::string &str) {
  for (auto it = str.begin(); it != str.end(); ++it) {
    *this *= kTen;
    *this += static_cast<uint8>(*it - '0');
  }
}

BigInt::BigInt(const std::vector<uint8> &byte_str) {
  for (auto it = byte_str.begin(); it != byte_str.end(); ++it) {
    *this <<= 8;
    *this |= static_cast<uint8>(*it);
  }
}

BigInt::BigInt(BufferType buffer) : buffer_(std::move(buffer)) {
}

uint64 BigInt::ToUInteger() const {
  uint64 t = 0;
  uint64 e = 1;
  for (const LimbType n : buffer_) {
    t += static_cast<uint64>(n) * e;
    e *= kBase;
  }
  return t;
}

std::string BigInt::ToString() const {
  std::string str;
  BigInt x(*this);
  while (x > kZero) {
    auto r = ExpDivide(x, kTen);
    str.push_back(static_cast<char>(r.second.ToUInteger()) + '0');
    x = std::move(r.first);
  }
  if (str.empty())
    str.push_back('0');
  else
    std::reverse(str.begin(), str.end());
  return str;
}

std::vector<uint8> BigInt::ToByteString() const {
  std::vector<uint8> byte_str;
  BigInt x(*this);
  while (x > kZero) {
    byte_str.push_back(static_cast<uint8>((x & 0xff).ToUInteger()));
    x >>= 8;
  }
  std::reverse(byte_str.begin(), byte_str.end());
  return byte_str;
}

BigInt &BigInt::operator=(uint64 n) {
  buffer_ = FromUInteger(n);
  return Normalize();
}

BigInt &BigInt::operator+=(const BigInt &o) {
  auto dst = buffer_.begin();
  auto src = o.buffer_.cbegin();
  DoubleLimbType sum = 0;
  while (dst != buffer_.end() || src != o.buffer_.cend()) {
    if (dst != buffer_.end()) {
      sum += *dst;
    } else {
      buffer_.push_back(0);
      dst = buffer_.end() - 1;
    }
    if (src != o.buffer_.cend()) {
      sum += *src;
      ++src;
    }
    *dst = static_cast<LimbType>(sum);
    ++dst;
    sum >>= kBaseBits;
  }
  if (sum != 0)
    buffer_.push_back(1);
  return Normalize();
}

BigInt &BigInt::operator-=(const BigInt &o) {
  if (o >= *this) {
    buffer_.clear();
    return *this;
  }
  auto dst = buffer_.begin();
  auto src = o.buffer_.cbegin();
  SignedDoubleLimbType sum = 0;
  while (dst != buffer_.end() || src != o.buffer_.cend()) {
    if (dst != buffer_.end()) {
      sum += *dst;
      ++dst;
    }
    if (src != o.buffer_.cend()) {
      sum -= *src;
      ++src;
    }
    if (sum < 0) {
      *(dst - 1) = static_cast<LimbType>(sum + kBase);
      sum = -1;
    } else {
      *(dst - 1) = static_cast<LimbType>(sum);
      sum = 0;
    }
  }
  return Normalize();
}

BigInt &BigInt::operator*=(const BigInt &o) {
  if (*this == kZero)
    return *this;
  if (o == kZero) {
    buffer_.clear();
    return *this;
  }
  BufferType result(buffer_.size() + o.buffer_.size() + 1);
  for (size_t i = 0; i < o.buffer_.size(); i++) {
    DoubleLimbType carry = 0;
    for (size_t j = 0; j < buffer_.size(); j++) {
      carry += (static_cast<DoubleLimbType>(o.buffer_[i]) * buffer_[j]) +
               result[i + j];
      result[i + j] = static_cast<LimbType>(carry);
      carry >>= kBaseBits;
    }
    result[i + buffer_.size()] = static_cast<LimbType>(carry);
  }
  std::swap(buffer_, result);
  return Normalize();
}

BigInt &BigInt::operator/=(const BigInt &o) {
  std::tie(*this, std::ignore) = ExpDivide(*this, o);
  return *this;
}

BigInt &BigInt::operator%=(const BigInt &o) {
  if (*this < o) {
    return *this;
  }
  std::tie(std::ignore, *this) = ExpDivide(*this, o);
  return *this;
}

BigInt &BigInt::operator&=(const BigInt &o) {
  auto it_a = buffer_.begin();
  auto it_b = o.buffer_.begin();
  for (; it_a != buffer_.end() && it_b != o.buffer_.end(); ++it_a, ++it_b) {
    *it_a &= *it_b;
  }
  if (it_a != buffer_.end())
    buffer_.erase(it_a, buffer_.end());
  return Normalize();
}

BigInt &BigInt::operator|=(const BigInt &o) {
  auto it_a = buffer_.begin();
  auto it_b = o.buffer_.begin();
  for (; it_a != buffer_.end() && it_b != o.buffer_.end(); ++it_a, ++it_b) {
    *it_a |= *it_b;
  }
  if (it_b != buffer_.end())
    buffer_.insert(buffer_.end(), it_b, o.buffer_.end());
  return Normalize();
}

BigInt &BigInt::operator<<=(size_t k) {
  const size_t bytes = k / kBaseBits;
  const size_t rem = k % kBaseBits;
  const size_t shift = kBaseBits - rem;
  const LimbType mask = ~static_cast<LimbType>((1ul << shift) - 1);
  LimbType tre = 0;
  for (auto it = buffer_.begin(); it != buffer_.end(); ++it) {
    const LimbType ntre = (*it) & mask;
    (*it) <<= rem;
    (*it) |= (tre >> shift);
    tre = ntre;
  }
  const LimbType left = tre >> shift;
  if (left != 0)
    buffer_.emplace_back(left);
  if (bytes != 0 && !buffer_.empty())
    buffer_.insert(buffer_.begin(), bytes, LimbType());
  return Normalize();
}

BigInt &BigInt::operator>>=(size_t k) {
  const size_t bytes = k / kBaseBits;
  const size_t rem = k % kBaseBits;
  const size_t shift = kBaseBits - rem;
  for (size_t i = 0; i < bytes && !buffer_.empty(); i++)
    buffer_.erase(buffer_.begin());
  const LimbType mask = static_cast<LimbType>((1ul << rem) - 1);
  LimbType tre = 0;
  for (auto it = buffer_.rbegin(); it != buffer_.rend(); ++it) {
    const LimbType ntre = (*it) & mask;
    (*it) >>= rem;
    (*it) |= (tre << shift);
    tre = ntre;
  }
  return Normalize();
}

bool BigInt::operator<(const BigInt &o) const {
  if (buffer_.size() != o.buffer_.size())
    return buffer_.size() < o.buffer_.size();
  for (auto it_a = buffer_.rbegin(), it_b = o.buffer_.rbegin();
       it_a != buffer_.rend(); ++it_a, ++it_b) {
    if (*it_a != *it_b)
      return *it_a < *it_b;
  }
  return false;
}

bool BigInt::operator<=(const BigInt &o) const {
  if (buffer_.size() != o.buffer_.size())
    return buffer_.size() < o.buffer_.size();
  for (auto it_a = buffer_.rbegin(), it_b = o.buffer_.rbegin();
       it_a != buffer_.rend(); ++it_a, ++it_b) {
    if (*it_a != *it_b)
      return *it_a <= *it_b;
  }
  return true;
}

bool BigInt::operator==(const BigInt &o) const {
  if (buffer_.size() != o.buffer_.size())
    return false;
  for (auto it_a = buffer_.rbegin(), it_b = o.buffer_.rbegin();
       it_a != buffer_.rend(); ++it_a, ++it_b) {
    if (*it_a != *it_b)
      return false;
  }
  return true;
}

BigInt &BigInt::Normalize() {
  while (!buffer_.empty() && buffer_.back() == 0)
    buffer_.pop_back();
  return *this;
}

// static
std::pair<BigInt, BigInt> BigInt::ExpDivide(const BigInt &x, const BigInt &y) {
  if (y == kZero)
    throw std::domain_error("Division by zero");
  if (x == kZero)
    return std::make_pair(kZero, kZero);
  if (y > x)
    return std::make_pair(kZero, x);
  if (y == x)
    return std::make_pair(kOne, kZero);
  size_t y_order = y.buffer_.size() - 1;
  BigInt r = x;
  size_t r_order = r.buffer_.size() - 1;
  BigInt tmp;
  BigInt result;
  bool r_neg = false;
  bool first_pass = true;
  do {
    LimbType guess;
    if (r.buffer_[r_order] <= y.buffer_[y_order] && r_order > 0) {
      const DoubleLimbType a =
          (static_cast<DoubleLimbType>(r.buffer_[r_order]) << kBaseBits) |
          r.buffer_[r_order - 1];
      const DoubleLimbType b = y.buffer_[y_order];
      const DoubleLimbType v = a / b;
      if (v > std::numeric_limits<LimbType>::max()) {
        guess = 1;
      } else {
        guess = static_cast<LimbType>(v);
        --r_order;
      }
    } else if (r_order == 0) {
      guess = r.buffer_[0] / y.buffer_[y_order];
    } else {
      const DoubleLimbType a =
          (static_cast<DoubleLimbType>(r.buffer_[r_order]) << kBaseBits) |
          r.buffer_[r_order - 1];
      const DoubleLimbType b =
          y_order > 0
              ? (static_cast<DoubleLimbType>(y.buffer_[y_order]) << kBaseBits) |
                    y.buffer_[y_order - 1]
              : (static_cast<DoubleLimbType>(y.buffer_[y_order]) << kBaseBits);
      const DoubleLimbType v = a / b;
      guess = static_cast<LimbType>(v);
    }
    size_t shift = r_order - y_order;
    if (result.buffer_.size() < shift + 1) {
      result.buffer_.insert(result.buffer_.end(),
                            shift + 1 - result.buffer_.size(), LimbType());
    }
    if (r_neg) {
      if (result.buffer_[shift] > guess) {
        result.buffer_[shift] -= guess;
      } else {
        tmp.buffer_.resize(shift + 1);
        tmp.buffer_[shift] = guess;
        for (size_t i = 0; i < shift; i++)
          tmp.buffer_[i] = 0;
        result -= tmp;
      }
    } else if (std::numeric_limits<LimbType>::max() - result.buffer_[shift] >
               guess) {
      result.buffer_[shift] += guess;
    } else {
      tmp.buffer_.resize(shift + 1);
      tmp.buffer_[shift] = guess;
      for (size_t i = 0; i < shift; i++)
        tmp.buffer_[i] = 0;
      result += tmp;
    }
    DoubleLimbType carry = 0;
    tmp.buffer_.resize(y.buffer_.size() + shift + 1);
    for (size_t i = 0; i < shift; i++)
      tmp.buffer_[i] = 0;
    for (size_t i = 0; i < y.buffer_.size(); i++) {
      carry += static_cast<DoubleLimbType>(y.buffer_[i]) *
               static_cast<DoubleLimbType>(guess);
      tmp.buffer_[i + shift] = static_cast<LimbType>(carry);
      carry >>= kBaseBits;
    }
    if (carry) {
      tmp.buffer_[tmp.buffer_.size() - 1] = static_cast<LimbType>(carry);
    } else {
      tmp.buffer_.pop_back();
    }
    if (r >= tmp) {
      r -= tmp;
    } else {
      std::swap(r, tmp);
      r -= tmp;
      r_neg = !r_neg;
    }
    if (first_pass)
      result.Normalize();
    r_order = r.buffer_.empty() ? 0 : r.buffer_.size() - 1;
    if (r_order < y_order)
      break;
  } while (r_order > y_order || r >= y);
  if (r_neg) {
    --result;
    r = y - r;
  }
  return std::make_pair(result.Normalize(), r.Normalize());
}

// static
size_t BigInt::BitCount(const BigInt &n) {
  if (n.buffer_.empty())
    return 0;
  size_t bits = (n.buffer_.size() - 1) * kBaseBits;
  LimbType k = n.buffer_.back();
  while (k > 0) {
    bits++;
    k >>= 1;
  }
  return bits;
}

// static
size_t BigInt::LSB(const BigInt &n) {
  if (n == kZero)
    throw std::underflow_error("No bits were set");
  size_t index = 0;
  BigInt mask = kOne;
  while ((n & mask) == kZero) {
    ++index;
    mask <<= 1;
  }
  return index;
}

static bool IsProbablyPrime(const BigInt &n) {
  const BigInt nm1 = n - kOne;
  BigInt r = nm1;
  const size_t k = BigInt::LSB(r);
  r >>= k;

  std::uniform_int_distribution<BigInt> dist(2, n - 2);
  for (size_t i = 0; i < kMillerRabinTrials; ++i) {
    const BigInt x = dist(LocalRNG());
    BigInt y = BigInt::ModularPow(x, r, n);
    size_t j = 0;
    for (;;) {
      if (y == nm1)
        break;
      if (y == kOne) {
        if (j == 0)
          break;
        return false;
      }
      if (++j == k)
        return false;
      y = (y * y) % n;
    }
  }
  return true;
}

// static
BigInt BigInt::Random(size_t bitlen) {
  if (bitlen == 0)
    throw std::underflow_error("Bitlength cannot be 0");
  std::uniform_int_distribution<BigInt> dist(0, kOne << bitlen);
  return dist(LocalRNG());
}

static bool ShiftToProbablyPrime(BigInt *n) {
  std::vector<uint64> mod_result(prime_list.size());
  for (size_t i = 1; i < prime_list.size(); ++i)
    mod_result[i] = (*n % prime_list[i]).ToUInteger();
  uint64 delta = 0;
  for (size_t i = 1; i < prime_list.size();) {
    if (((mod_result[i] + delta) % prime_list[i]) <= 1) {
      delta += 2;
      if (delta > std::numeric_limits<uint32>::max())
        return false;
      i = 1;
    } else {
      ++i;
    }
  }
  (*n) += delta;
  return true;
}

// static
BigInt BigInt::RandomPrime(size_t bitlen, bool verbose) {
  std::call_once(prime_list_once, &InitPrimeList);
  if (bitlen == 0)
    throw std::underflow_error("Bitlength cannot be 0");
  BigInt n;
  for (;;) {
    n = Random(bitlen);
    n |= kOne;
    n |= (kOne << (bitlen - 1));
    if (verbose)
      std::cout << "." << std::flush;
    if (!ShiftToProbablyPrime(&n))
      continue;
    if (IsProbablyPrime(n))
      break;
  }
  if (verbose)
    std::cout << "+++" << std::endl;
  return n;
}

// static
BigInt BigInt::ModularPow(BigInt base, BigInt exponent, const BigInt &modulus) {
  BigInt c(kOne);
  if (modulus == c)
    return kZero;
  base %= modulus;
  while (exponent > kZero) {
    if ((exponent & kOne) == kOne) {
      c *= base;
      c %= modulus;
    }
    base *= base;
    base %= modulus;
    exponent >>= 1;
  }
  return c;
}

// static
BigInt BigInt::ModularInverse(const BigInt &a, const BigInt &b) {
  BigSInt t = kSZero;
  BigSInt newt = kSOne;
  BigSInt r = b;
  BigSInt newr = a;
  while (newr != kSZero) {
    const BigSInt q = r / newr;
    std::tie(t, newt) = std::make_pair(newt, t - q * newt);
    std::tie(r, newr) = std::make_pair(newr, r - q * newr);
  }
  if (t < kSZero)
    t += b;
  return t.ToUnsigned();
}

// static
BigInt BigInt::GCD(BigInt a, BigInt b) {
  while (b != kZero) {
    BigInt r = a % b;
    a = b;
    b = r;
  }
  return a;
}

// static
BigInt::BufferType BigInt::FromUInteger(uint64 n) {
  BufferType t;
  while (n > 0) {
    t.push_back(static_cast<LimbType>(n % kBase));
    n /= kBase;
  }
  return t;
}

BigSInt::BigSInt(int64 n)
    : sign_(n < 0), num_(static_cast<uint64>(std::abs(n))) {
}

BigSInt::BigSInt(const BigInt &n) : sign_(false), num_(n) {
}

BigSInt &BigSInt::operator+=(const BigSInt &o) {
  if (sign_) {
    sign_ = false;
    *this -= o;
    sign_ = !sign_;
  } else if (o.sign_) {
    *this -= -o;
  } else {
    num_ += o.num_;
  }
  return Normalize();
}

BigSInt &BigSInt::operator-=(const BigSInt &o) {
  if (sign_) {
    sign_ = false;
    *this += o;
    sign_ = !sign_;
  } else if (o.sign_) {
    *this += -o;
  } else if (*this < o) {
    BigSInt t(o);
    t -= *this;
    *this = -t;
  } else {
    num_ -= o.num_;
  }
  return Normalize();
}

BigSInt &BigSInt::operator*=(const BigSInt &o) {
  num_ *= o.num_;
  sign_ = sign_ != o.sign_;
  return Normalize();
}

BigSInt &BigSInt::operator/=(const BigSInt &o) {
  num_ /= o.num_;
  sign_ = sign_ != o.sign_;
  return Normalize();
}

BigSInt &BigSInt::Normalize() {
  if (*this == kSZero)
    sign_ = false;
  return *this;
}

BigSInt BigSInt::operator-() const {
  if (*this == kSZero)
    return *this;
  BigSInt t(*this);
  t.sign_ = !t.sign_;
  return t;
}

bool BigSInt::operator<(const BigSInt &o) const {
  if (sign_ != o.sign_)
    return sign_;
  return num_ < o.num_;
}

bool BigSInt::operator==(const BigSInt &o) const {
  return num_ == o.num_ && sign_ == o.sign_;
}

}  // namespace pud
