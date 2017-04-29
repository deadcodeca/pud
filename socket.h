// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "util.h"

namespace pud {

class Endpoint {
 public:
  Endpoint() : addr_() {}

  explicit Endpoint(const struct sockaddr_in &addr) : addr_(addr) {}

  Endpoint(const std::string &address, uint16 port)
      : Endpoint(inet_addr(address.c_str()), port) {}

  explicit Endpoint(const std::string &endpoint_str);

  Endpoint(in_addr_t address, uint16 port);

  bool operator==(const Endpoint &that) const {
    return std::tie(addr_.sin_addr.s_addr, addr_.sin_port) ==
           std::tie(that.addr_.sin_addr.s_addr, addr_.sin_port);
  }
  bool operator!=(const Endpoint &that) const { return !(*this == that); }

  const struct sockaddr *sockaddr() const {
    return reinterpret_cast<const struct sockaddr *>(&addr_);
  }

  socklen_t size() const { return sizeof(addr_); }

  in_addr_t address() const { return addr_.sin_addr.s_addr; }
  uint16 port() const { return ntohs(addr_.sin_port); }

  std::string ToString() const;

  friend std::ostream &operator<<(std::ostream &os, const Endpoint &ep) {
    os << inet_ntoa(ep.addr_.sin_addr) << ":" << htons(ep.addr_.sin_port);
    return os;
  }

 private:
  struct sockaddr_in addr_;
};

class Pollable {
 public:
  enum Flag : uint32 {
    INPUT = 1 << 0,
    OUTPUT = 1 << 1,
    HUP = 1 << 2,
  };

  typedef std::function<void(uint32)> Callback;

  Pollable(uint32 flag, int32 fd) : flag_(flag), fd_(fd) {}
  Pollable(uint32 flag, int32 fd, Callback callback)
      : flag_(flag), fd_(fd), callback_(std::move(callback)) {}

  uint32 flag() const { return flag_; }
  int32 fd() const { return fd_; }
  const Callback &callback() const { return callback_; }

 private:
  const uint32 flag_;
  const int32 fd_;
  const Callback callback_;
};

void Poll(const std::vector<Pollable> &items, int64 timeout_msec);

void SocketNonBlocking(int32 fd);

}  // namespace pud
