// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <ostream>
#include <string>
#include <tuple>
#include "socket.h"
#include "util.h"

namespace pud {

class Server {
 public:
  static const size_t kMaxPacketSize;
  typedef std::function<void(const Endpoint &, const std::string &)>
      ReadCallback;

  explicit Server(uint16 port);
  ~Server();

  void Listen();
  void Send(const Endpoint &endpoint, const std::string &buf);
  Pollable ReadEvent(ReadCallback callback);
  void Close();

  uint16 port() const { return port_; }
  const Endpoint &server_endpoint() const { return server_endpoint_; }
  bool closed() const { return fd_ < 0; }

  Server(const Server &) = delete;
  void operator=(const Server &) = delete;

 private:
  void Recv(ReadCallback callback);

  const uint16 port_;
  const Endpoint server_endpoint_;
  int32 fd_;
};

}  // namespace pud
