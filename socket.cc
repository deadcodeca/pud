// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "socket.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <vector>
#include "exception.h"
#include "util.h"

namespace pud {

Endpoint::Endpoint(const std::string &endpoint_str) : addr_() {
  const size_t pos = endpoint_str.find(':');
  if (pos == std::string::npos) {
    throw InternalError("Invalid endpoint, must be in the format of ip:port");
  }
  const std::string address_str = endpoint_str.substr(0, pos);
  const uint16 port =
      static_cast<uint16>(std::stoi(endpoint_str.substr(pos + 1)));
  const in_addr_t address = inet_addr(address_str.c_str());
  if (address == INADDR_NONE)
    throw InternalError("Invalid address specified");
  addr_.sin_family = AF_INET;
  addr_.sin_addr.s_addr = address;
  addr_.sin_port = htons(port);
}

Endpoint::Endpoint(in_addr_t address, uint16 port) : addr_() {
  if (address == INADDR_NONE)
    throw InternalError("Invalid address specified");
  addr_.sin_family = AF_INET;
  addr_.sin_addr.s_addr = address;
  addr_.sin_port = htons(port);
}

std::string Endpoint::ToString() const {
  std::stringstream ss;
  ss << *this;
  return ss.str();
}

static int16 PollableFlagToPollEvent(uint32 flag) {
  int16 event = 0;
  if ((flag & Pollable::INPUT) != 0)
    event |= POLLIN;
  if ((flag & Pollable::OUTPUT) != 0)
    event |= POLLOUT;
  if ((flag & Pollable::HUP) != 0)
    event |= POLLHUP;
  return event;
}

static uint32 PollEventToPollableFlag(int16 event) {
  uint32 flag = 0;
  if ((event & POLLIN) != 0)
    flag |= Pollable::INPUT;
  if ((event & POLLOUT) != 0)
    flag |= Pollable::OUTPUT;
  if ((event & POLLHUP) != 0)
    flag |= Pollable::HUP;
  return flag;
}

void Poll(const std::vector<Pollable> &items, int64 timeout_msec) {
  struct pollfd fds[items.size()];
  nfds_t index = 0;
  for (const Pollable &item : items) {
    fds[index].fd = item.fd();
    fds[index].events = PollableFlagToPollEvent(item.flag());
    fds[index].revents = 0;
    index++;
  }
  const int ret =
      poll(fds, index, static_cast<int32>(timeout_msec & 0x7fffffff));
  if (ret < 0)
    throw SystemError("Poll failed");
  index = 0;
  for (const Pollable &item : items) {
    const uint32 flag = PollEventToPollableFlag(fds[index].revents);
    if (flag != 0) {
      if (item.callback())
        item.callback()(flag);
    }
    index++;
  }
}

void SocketNonBlocking(int32 fd) {
  int32 flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    throw SystemError("Failed to get socket flags");
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) != 0) {
    throw SystemError("Failed to set socket as non-blocking");
  }
}

}  // namespace pud
