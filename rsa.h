// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <limits>
#include <utility>
#include <vector>
#include "bignum.h"
#include "proto.h"
#include "sha256.h"
#include "util.h"

namespace pud {

class RSAPublic {
 public:
  RSAPublic(const BigInt &e, const BigInt &n) : e_(e), n_(n) {}

  BigInt Encrypt(BigInt m) const { return BigInt::ModularPow(m, e_, n_); }

  const BigInt &e() const { return e_; }
  const BigInt &r() const { return e_; }
  const BigInt &n() const { return n_; }

  bool operator==(const RSAPublic &that) const {
    return e_ == that.e_ && n_ == that.n_;
  }

  bool operator!=(const RSAPublic &that) const { return !(*this == that); }

 private:
  BigInt e_;
  BigInt n_;
};

class RSAPrivate {
 public:
  RSAPrivate(const BigInt &d, const BigInt &n) : d_(d), n_(n) {}

  BigInt Decrypt(BigInt m) const { return BigInt::ModularPow(m, d_, n_); }

  const BigInt &d() const { return d_; }
  const BigInt &r() const { return d_; }
  const BigInt &n() const { return n_; }

  bool operator==(const RSAPrivate &that) const {
    return d_ == that.d_ && n_ == that.n_;
  }

  bool operator!=(const RSAPrivate &that) const { return !(*this == that); }

 private:
  BigInt d_;
  BigInt n_;
};

std::pair<RSAPublic, RSAPrivate> MakeRSAKey(uint64 bitlen, bool verbose);

std::vector<uint8> SignMessage(const RSAPrivate &priv, const SHA256 &ctx);

void WriteSignature(const RSAPrivate &priv, const SHA256 &ctx,
                    OutputBuffer *out);

bool VerifyMessage(const RSAPublic &pub, const SHA256 &ctx,
                   const std::vector<uint8> &signed_block);

bool VerifySignature(const RSAPublic &pub, const SHA256 &ctx, InputBuffer *in);

template <typename Key>
void WriteRSAKey(const Key &key, OutputBuffer *buf) {
  const std::vector<uint8> r_enc = key.r().ToByteString();
  const std::vector<uint8> n_enc = key.n().ToByteString();
  buf->PushVariableLength(r_enc.size());
  buf->Push(r_enc);
  buf->PushVariableLength(n_enc.size());
  buf->Push(n_enc);
}

template <typename Key>
Key ReadRSAKey(InputBuffer *buf) {
  const size_t r_len = buf->PopVariableLength();
  const BigInt r = BigInt(buf->Pop(r_len));
  const size_t n_len = buf->PopVariableLength();
  const BigInt n = BigInt(buf->Pop(n_len));
  return Key(r, n);
}

}  // namespace pud
