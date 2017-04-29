// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "server.h"

#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <vector>
#include "exception.h"
#include "socket.h"
#include "util.h"

namespace pud {
const size_t Server::kMaxPacketSize = 65536;

Server::Server(uint16 port)
    : port_(port), server_endpoint_(INADDR_ANY, port), fd_(-1) {
}

Server::~Server() {
  Close();
}

void Server::Listen() {
  Close();
  if ((fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    throw SystemError("Failed to create socket");
  SocketNonBlocking(fd_);
  if (bind(fd_, server_endpoint_.sockaddr(), server_endpoint_.size()) < 0) {
    throw SystemError("Failed to bind to port");
  }
}

void Server::Send(const Endpoint &endpoint, const std::string &buf) {
  assert(fd_ >= 0);
  assert(buf.size() <= kMaxPacketSize);
  ssize_t datalen = sendto(fd_, buf.data(), buf.size(), 0, endpoint.sockaddr(),
                           endpoint.size());
  if (datalen < 0) {
    throw SystemError("Failed to send packet to host: ");
  }
  assert(static_cast<size_t>(datalen) == buf.size());
}

Pollable Server::ReadEvent(ReadCallback callback) {
  return Pollable(Pollable::Flag::INPUT, fd_,
                  std::bind(&Server::Recv, this, callback));
}

void Server::Recv(ReadCallback callback) {
  assert(fd_ >= 0);
  struct sockaddr_in new_client;
  socklen_t nlen = sizeof(new_client);
  std::vector<char> tmp_buf(kMaxPacketSize);
  const ssize_t datalen =
      recvfrom(fd_, tmp_buf.data(), tmp_buf.size(), MSG_DONTWAIT,
               (struct sockaddr *)&new_client, &nlen);
  if (datalen < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return;
    throw SystemError("Failed to recv packet from host");
  }
  callback(Endpoint(new_client),
           std::string(tmp_buf.begin(), tmp_buf.begin() + datalen));
}

void Server::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

}  // namespace pud
