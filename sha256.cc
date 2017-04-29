// Copyright (C) 2016, All rights reserved.
// Author: contem
//
// This is based on SHA256 implementation in LibTomCrypt that was released into
// public domain by Tom St Denis.

#include "sha256.h"

#include <string.h>
#include <algorithm>
#include "util.h"

#define RORC(x, y)                                                             \
  ((((static_cast<uint32>(x) & 0xfffffffful) >> static_cast<uint32>((y)&31)) | \
  (static_cast<uint32>(x) << static_cast<uint32>(32 - ((y)&31)))) &            \
  0xfffffffful)

#define CH(x, y, z) (z ^ (x & (y ^ z)))
#define MAJ(x, y, z) (((x | y) & z) | (x &\
                                       y))
#define S(x, n) RORC((x), (n))
#define R(x, n) (((x)&0xfffffffful) >> (n))
#define SIGMA(x) (S(x, 2) ^ S(x, 13) ^ S(x, 22))
#define SIGMA1(x) (S(x, 6) ^ S(x, 11) ^ S(x, 25))
#define GAMMA(x) (S(x, 7) ^ S(x, 18) ^ R(x, 3))
#define GAMMA1(x) (S(x, 17) ^ S(x, 19) ^ R(x, 10))

#define RND(a, b, c, d, e, f, g, h, i)                    \
  do {                                                    \
    uint32 x = h + SIGMA1(e) + CH(e, f, g) + K[i] + W[i]; \
    uint32 y = SIGMA(a) + MAJ(a, b, c);                   \
    d += x;                                               \
    h = x + y;                                            \
  } while (0)

#define GET_BE32(a)                        \
  (((static_cast<uint32>((a)[0])) << 24) | \
  ((static_cast<uint32>((a)[1])) << 16) |  \
  ((static_cast<uint32>((a)[2])) << 8) |   \
  (static_cast<uint32>((a)[3])))

#define PUT_BE32(a, val)                                                    \
  do {                                                                      \
    (a)[0] = static_cast<uint8>(((static_cast<uint32>(val)) >> 24) & 0xff); \
    (a)[1] = static_cast<uint8>(((static_cast<uint32>(val)) >> 16) & 0xff); \
    (a)[2] = static_cast<uint8>(((static_cast<uint32>(val)) >> 8) & 0xff);  \
    (a)[3] = static_cast<uint8>((static_cast<uint32>(val)) & 0xff);         \
  } while (0)

#define PUT_BE64(a, val)                                            \
  do {                                                              \
    (a)[0] = static_cast<uint8>((static_cast<uint64>(val)) >> 56);  \
    (a)[1] = static_cast<uint8>((static_cast<uint64>(val)) >> 48);  \
    (a)[2] = static_cast<uint8>((static_cast<uint64>(val)) >> 40);  \
    (a)[3] = static_cast<uint8>((static_cast<uint64>(val)) >> 32);  \
    (a)[4] = static_cast<uint8>((static_cast<uint64>(val)) >> 24);  \
    (a)[5] = static_cast<uint8>((static_cast<uint64>(val)) >> 16);  \
    (a)[6] = static_cast<uint8>((static_cast<uint64>(val)) >> 8);   \
    (a)[7] = static_cast<uint8>((static_cast<uint64>(val)) & 0xff); \
  } while (0)

namespace pud {

static const uint32 K[64] = {
    0x428a2f98ul, 0x71374491ul, 0xb5c0fbcful, 0xe9b5dba5ul, 0x3956c25bul,
    0x59f111f1ul, 0x923f82a4ul, 0xab1c5ed5ul, 0xd807aa98ul, 0x12835b01ul,
    0x243185beul, 0x550c7dc3ul, 0x72be5d74ul, 0x80deb1feul, 0x9bdc06a7ul,
    0xc19bf174ul, 0xe49b69c1ul, 0xefbe4786ul, 0x0fc19dc6ul, 0x240ca1ccul,
    0x2de92c6ful, 0x4a7484aaul, 0x5cb0a9dcul, 0x76f988daul, 0x983e5152ul,
    0xa831c66dul, 0xb00327c8ul, 0xbf597fc7ul, 0xc6e00bf3ul, 0xd5a79147ul,
    0x06ca6351ul, 0x14292967ul, 0x27b70a85ul, 0x2e1b2138ul, 0x4d2c6dfcul,
    0x53380d13ul, 0x650a7354ul, 0x766a0abbul, 0x81c2c92eul, 0x92722c85ul,
    0xa2bfe8a1ul, 0xa81a664bul, 0xc24b8b70ul, 0xc76c51a3ul, 0xd192e819ul,
    0xd6990624ul, 0xf40e3585ul, 0x106aa070ul, 0x19a4c116ul, 0x1e376c08ul,
    0x2748774cul, 0x34b0bcb5ul, 0x391c0cb3ul, 0x4ed8aa4aul, 0x5b9cca4ful,
    0x682e6ff3ul, 0x748f82eeul, 0x78a5636ful, 0x84c87814ul, 0x8cc70208ul,
    0x90befffaul, 0xa4506cebul, 0xbef9a3f7ul, 0xc67178f2ul};

SHA256::SHA256()
    : curlen_(0), length_(0) {
  state_[0] = 0x6a09e667ul;
  state_[1] = 0xbb67ae85ul;
  state_[2] = 0x3c6ef372ul;
  state_[3] = 0xa54ff53aul;
  state_[4] = 0x510e527ful;
  state_[5] = 0x9b05688cul;
  state_[6] = 0x1f83d9abul;
  state_[7] = 0x5be0cd19ul;
}

void SHA256::Transform(const uint8 *buf) {
  uint32 S[8];
  for (size_t i = 0; i < 8; i++)
    S[i] = state_[i];

  uint32 W[64];
  for (size_t i = 0; i < 16; i++)
    W[i] = GET_BE32(buf + (4 * i));
  for (size_t i = 16; i < 64; i++)
    W[i] = GAMMA1(W[i - 2]) + W[i - 7] + GAMMA(W[i - 15]) + W[i - 16];

  for (size_t i = 0; i < 64; ++i) {
    RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i);
    uint32 t = S[7];
    S[7] = S[6];
    S[6] = S[5];
    S[5] = S[4];
    S[4] = S[3];
    S[3] = S[2];
    S[2] = S[1];
    S[1] = S[0];
    S[0] = t;
  }

  for (size_t i = 0; i < 8; i++)
    state_[i] = state_[i] + S[i];
}

void SHA256::Update(std::vector<uint8>::const_iterator begin,
                    std::vector<uint8>::const_iterator end) {
  const uint8 *in = &(*begin);
  size_t inlen = static_cast<size_t>(std::distance(begin, end));
  while (inlen > 0) {
    if (curlen_ == 0 && inlen >= sizeof(buf_)) {
      Transform(in);
      length_ += sizeof(buf_) * 8;
      in += sizeof(buf_);
      inlen -= sizeof(buf_);
    } else {
      size_t n = std::min(inlen, (sizeof(buf_) - curlen_));
      memcpy(buf_ + curlen_, in, n);
      curlen_ += n;
      in += n;
      inlen -= n;
      if (curlen_ == sizeof(buf_)) {
        Transform(buf_);
        length_ += 8 * sizeof(buf_);
        curlen_ = 0;
      }
    }
  }
}

void SHA256::Final() {
  length_ += curlen_ * 8;
  buf_[curlen_++] = 0x80;
  if (curlen_ > 56) {
    while (curlen_ < sizeof(buf_))
      buf_[curlen_++] = 0;
    Transform(buf_);
    curlen_ = 0;
  }
  while (curlen_ < 56)
    buf_[curlen_++] = 0;
  PUT_BE64(buf_ + 56, length_);
  Transform(buf_);
  for (size_t i = 0; i < 8; i++)
    PUT_BE32(hash_ + (4 * i), state_[i]);
}

SHA256 Hash(std::vector<uint8>::const_iterator begin,
            std::vector<uint8>::const_iterator end) {
  SHA256 hash;
  hash.Update(begin, end);
  hash.Final();
  return hash;
}

}  // namespace pud
