// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <algorithm>
#include <memory>
#include "proto.h"
#include "rsa.h"
#include "socket.h"
#include "util.h"

namespace pud {

class Node {
 public:
  typedef std::shared_ptr<Node> Handle;
  typedef std::chrono::system_clock Clock;

  Node(const Endpoint &endpoint, const RSAPublic &key, uint64 ident,
       uint64 last_known_sequence, uint64 last_seen_ago = 0);

  const Endpoint &endpoint() const { return endpoint_; }
  void set_endpoint(const Endpoint &endpoint) { endpoint_ = endpoint; }

  const RSAPublic &key() const { return key_; }
  uint64 ident() const { return ident_; }

  uint64 last_known_sequence() const { return last_known_sequence_; }
  void set_last_known_sequence(uint64 seq) { last_known_sequence_ = seq; }

  uint64 last_seen_ago() const {
    return static_cast<uint64>(
        std::max(0L, std::chrono::duration_cast<std::chrono::seconds>(
                         Node::Clock::now() - last_seen_).count()));
  }
  void set_last_seen() { last_seen_ = Clock::now(); }

 private:
  Endpoint endpoint_;
  const RSAPublic key_;
  const uint64 ident_;
  uint64 last_known_sequence_;
  Clock::time_point last_seen_;
};

void WriteNode(const Node &node, OutputBuffer *buf);

Node ReadNode(InputBuffer *buf);

}  // namespace pud
