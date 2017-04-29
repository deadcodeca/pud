// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "node.h"

#include "util.h"

namespace pud {

Node::Node(const Endpoint &endpoint, const RSAPublic &key, uint64 ident,
           uint64 last_known_sequence, uint64 last_seen_ago)
    : endpoint_(endpoint),
      key_(key),
      ident_(ident),
      last_known_sequence_(last_known_sequence),
      last_seen_(Clock::now() - std::chrono::seconds(last_seen_ago)) {
}

void WriteNode(const Node &node, OutputBuffer *buf) {
  buf->PushU64(node.ident());
  buf->PushU32(node.endpoint().address());
  buf->PushU16(node.endpoint().port());
  buf->PushU64(node.last_known_sequence());
  buf->PushU64(node.last_seen_ago());
  WriteRSAKey(node.key(), buf);
}

Node ReadNode(InputBuffer *buf) {
  const uint64 ident = buf->PopU64();
  const in_addr_t address = buf->PopU32();
  const uint16 port = buf->PopU16();
  const uint64 last_known_sequence = buf->PopU64();
  const uint64 last_seen_ago = buf->PopU64();
  const RSAPublic key = ReadRSAKey<RSAPublic>(buf);
  return Node(Endpoint(address, port), key, ident, last_known_sequence,
              last_seen_ago);
}

}  // namespace pud
