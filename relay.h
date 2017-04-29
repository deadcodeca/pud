// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <memory>
#include <string>
#include "socket.h"
#include "util.h"

namespace pud {

class Relay {
 public:
  enum class Control { OPEN, WRITE, CLOSE };

  typedef std::shared_ptr<Relay> Handle;
  typedef std::function<void(Control, const std::string &)> ReadCallback;

  virtual ~Relay() = default;
  virtual void Send(const std::string &buf) = 0;
  virtual Pollable ReadEvent(ReadCallback callback) = 0;
  virtual void Close() = 0;
};

Relay::Handle NewUDPRelay(const Endpoint &endpoint);

Relay::Handle NewTCPRelay(const Endpoint &endpoint);

Relay::Handle NewCommandRelay(const std::string &cmd);

}  // namespace pud
