// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "relay.h"

#include <assert.h>
#include <fcntl.h>
#include <grp.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <vector>
#include "exception.h"
#include "server.h"
#include "socket.h"
#include "util.h"

namespace pud {
namespace {
const char kShellCommand[] = "/bin/sh";

class UDPRelay : public Relay {
 public:
  explicit UDPRelay(const Endpoint &endpoint) : endpoint_(endpoint) {}
  ~UDPRelay() override {}

  void Initialize();

  void Send(const std::string &buf) override;
  Pollable ReadEvent(ReadCallback callback) override;
  void Close() override;

 private:
  void Read(ReadCallback callback, uint32 flag);

  const Endpoint endpoint_;
  ScopedFd fd_;
};

class TCPRelay : public Relay {
 public:
  enum class State { PENDING, OPEN, READING, CLOSED };

  explicit TCPRelay(const Endpoint &endpoint)
      : endpoint_(endpoint), state_(State::PENDING) {}
  ~TCPRelay() override {}

  void Initialize();

  void Send(const std::string &buf) override;
  Pollable ReadEvent(ReadCallback callback) override;
  void Close() override;

 private:
  void RetryConnect(ReadCallback callback, uint32 flag);
  void Read(ReadCallback callback, uint32 flag);

  const Endpoint endpoint_;
  ScopedFd fd_;
  State state_;
};

class CommandRelay : public Relay {
 public:
  explicit CommandRelay(const std::string &cmd) : cmd_(cmd) {}
  ~CommandRelay() override {}

  void Initialize();

  void Send(const std::string &buf) override;
  Pollable ReadEvent(ReadCallback callback) override;
  void Close() override;

 private:
  static void ChildProcess(const char *pts_name, const std::string &cmd,
                           ScopedFd *slave_fd);

  void Read(ReadCallback callback, uint32 flag);

  const std::string cmd_;
  ScopedFd fd_;
};

void UDPRelay::Initialize() {
  fd_ = ScopedFd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (fd_ < 0) {
    throw SystemError("Failed to create socket");
  }
  SocketNonBlocking(fd_);
  const Endpoint my_addr(INADDR_ANY, 0);
  if (bind(fd_, my_addr.sockaddr(), my_addr.size()) < 0) {
    throw SystemError("Failed to bind to socket");
  }
}

void UDPRelay::Send(const std::string &buf) {
  assert(fd_ >= 0);
  const ssize_t datalen = sendto(fd_, buf.data(), buf.size(), 0,
                                 endpoint_.sockaddr(), endpoint_.size());
  if (datalen < 0) {
    throw UnknownError("Failed to send packet to host");
  }
  if (static_cast<size_t>(datalen) < buf.size()) {
    throw UnknownError(Concat("Failed to send entire packet, sent ", datalen,
                              " out of ", buf.size()));
  }
}

void UDPRelay::Read(ReadCallback callback, uint32 flag) {
  if ((flag & Pollable::HUP) != 0) {
    callback(Control::CLOSE, std::string());
    return;
  }
  if ((flag & Pollable::INPUT) == 0)
    return;
  assert(fd_ >= 0);
  struct sockaddr_in new_client;
  socklen_t nlen = sizeof(new_client);
  std::vector<char> tmp_buf(Server::kMaxPacketSize);
  ssize_t datalen = recvfrom(fd_, tmp_buf.data(), tmp_buf.size(), MSG_DONTWAIT,
                             (struct sockaddr *)&new_client, &nlen);
  if (datalen < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return;
    callback(Control::CLOSE,
             Concat("Failed to recv packet from host: ", strerror(errno)));
    return;
  }
  if (Endpoint(new_client) != endpoint_)
    return;
  callback(Control::WRITE,
           std::string(tmp_buf.begin(), tmp_buf.begin() + datalen));
}

Pollable UDPRelay::ReadEvent(ReadCallback callback) {
  return Pollable(
      Pollable::Flag::INPUT, fd_,
      std::bind(&UDPRelay::Read, this, callback, std::placeholders::_1));
}

void UDPRelay::Close() {
  fd_.Close();
}

void TCPRelay::Initialize() {
  fd_ = ScopedFd(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (fd_ < 0) {
    throw SystemError("Failed to create socket");
  }
  SocketNonBlocking(fd_);
  if (connect(fd_, endpoint_.sockaddr(), endpoint_.size()) < 0) {
    if (errno == EINPROGRESS) {
      state_ = State::PENDING;
    } else {
      throw SystemError("Failed to connect to relay");
    }
  } else {
    state_ = State::OPEN;
  }
}

void TCPRelay::RetryConnect(ReadCallback callback, uint32 flag) {
  if ((flag & Pollable::HUP) != 0) {
    callback(Control::CLOSE, "Connection failed");
  } else if ((flag & Pollable::OUTPUT) != 0) {
    if (state_ == State::PENDING) {
      if (connect(fd_, endpoint_.sockaddr(), endpoint_.size()) >= 0)
        state_ = State::OPEN;
    }
    if (state_ == State::OPEN) {
      callback(Control::OPEN, std::string());
      state_ = State::READING;
    }
  }
}

void TCPRelay::Send(const std::string &buf) {
  assert(fd_ >= 0);
  const ssize_t datalen = send(fd_, buf.data(), buf.size(), 0);
  if (datalen < 0) {
    throw SystemError("Failed to send packet to host");
  }
  if (static_cast<size_t>(datalen) < buf.size()) {
    throw UnknownError(Concat("Failed to send entire packet, sent ", datalen,
                              " out of ", buf.size()));
  }
}

void TCPRelay::Read(ReadCallback callback, uint32 flag) {
  if ((flag & Pollable::HUP) != 0) {
    callback(Control::CLOSE, std::string());
    return;
  }
  if ((flag & Pollable::INPUT) == 0)
    return;
  assert(fd_ >= 0);
  std::vector<char> tmp_buf(Server::kMaxPacketSize);
  const ssize_t datalen = recv(fd_, tmp_buf.data(), tmp_buf.size(), 0);
  if (datalen < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return;
    callback(Control::CLOSE,
             Concat("Failed to recv packet from host: ", strerror(errno)));
  } else if (datalen == 0) {
    callback(Control::CLOSE, "Connection closed");
  } else {
    callback(Control::WRITE,
             std::string(tmp_buf.begin(), tmp_buf.begin() + datalen));
  }
}

Pollable TCPRelay::ReadEvent(ReadCallback callback) {
  switch (state_) {
    case State::PENDING:
    case State::OPEN:
      return Pollable(Pollable::Flag::OUTPUT, fd_,
                      std::bind(&TCPRelay::RetryConnect, this, callback,
                                std::placeholders::_1));
    case State::READING:
      return Pollable(
          Pollable::Flag::INPUT, fd_,
          std::bind(&TCPRelay::Read, this, callback, std::placeholders::_1));
    case State::CLOSED:
      break;
  }
  return Pollable(0, fd_);
}

void TCPRelay::Close() {
  fd_.Close();
}

void CommandRelay::Initialize() {
  fd_ = ScopedFd(open("/dev/ptmx", O_RDWR | O_CLOEXEC));
  if (fd_.fd() < 0)
    throw SystemError("Failed to open /dev/ptmx");
  if (grantpt(fd_.fd()) < 0)
    throw SystemError("Failed to change ownership of pts");
  if (unlockpt(fd_.fd()) < 0)
    throw SystemError("Failed to unlock pts");

  char pts_name[256];
  if (ptsname_r(fd_.fd(), pts_name, sizeof(pts_name)) < 0)
    throw SystemError("Failed to fetch pts name");

  ScopedFd slave_fd(open(pts_name, O_RDWR | O_NOCTTY));
  if (slave_fd.fd() < 0)
    throw SystemError(Concat("Failed to open ", pts_name));
  ioctl(slave_fd.fd(), TIOCNXCL);

  ScopedChildPid child_pid(fork());
  if (child_pid.pid() < 0)
    throw SystemError("Failed to create child process");
  if (child_pid.pid() == 0) {
    ChildProcess(pts_name, cmd_, &slave_fd);
    _exit(-1);
  }
  slave_fd.Close();
  child_pid.Release();
}

// static
void CommandRelay::ChildProcess(const char *pts_name, const std::string &cmd,
                                ScopedFd *slave_fd) {
  int32 error_fd = slave_fd->fd();
  try {
    if (setsid() < 0)
      throw SystemError("Failed to set session leader");
    if (ioctl(slave_fd->fd(), TIOCSCTTY, nullptr) < 0)
      throw SystemError("Failed to set control terminal");
    slave_fd->DupeFd(STDIN_FILENO);
    slave_fd->DupeFd(STDOUT_FILENO);
    slave_fd->DupeFd(STDERR_FILENO);
    slave_fd->Close();
    error_fd = STDERR_FILENO;

    ScopedChildPid child_pid(fork());
    if (child_pid.pid() < 0)
      throw SystemError("Failed to fork");
    if (child_pid.pid() == 0) {
      const char *argv[] = {kShellCommand, "-c", cmd.c_str(), nullptr};
      execvp(argv[0], const_cast<char *const *>(argv));
      throw SystemError("Child process exited abnormally or failed to start");
    }
    child_pid.WaitCheckStatus();
  } catch (const Exception &error) {
    const std::string error_msg = Concat(error.what(), "\n");
    (void)write(error_fd, error_msg.data(), error_msg.size());
  }
}

void CommandRelay::Send(const std::string &buf) {
  assert(fd_ >= 0);
  const ssize_t datalen = write(fd_, buf.data(), buf.size());
  if (datalen < 0) {
    throw SystemError("Failed to send packet to host");
  }
  if (static_cast<size_t>(datalen) < buf.size()) {
    throw UnknownError(Concat("Failed to send entire packet, sent ", datalen,
                              " out of ", buf.size()));
  }
}

void CommandRelay::Read(ReadCallback callback, uint32 flag) {
  if ((flag & Pollable::HUP) != 0) {
    callback(Control::CLOSE, std::string());
    return;
  }
  if ((flag & Pollable::INPUT) == 0)
    return;
  assert(fd_ >= 0);
  std::vector<char> tmp_buf(Server::kMaxPacketSize);
  const ssize_t datalen = read(fd_, tmp_buf.data(), tmp_buf.size());
  if (datalen < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return;
    callback(Control::CLOSE,
             Concat("Failed to read from pipe: ", strerror(errno)));
  } else if (datalen == 0) {
    callback(Control::CLOSE, "End of stream");
  } else {
    callback(Control::WRITE,
             std::string(tmp_buf.begin(), tmp_buf.begin() + datalen));
  }
}

Pollable CommandRelay::ReadEvent(ReadCallback callback) {
  return Pollable(
      Pollable::Flag::INPUT, fd_,
      std::bind(&CommandRelay::Read, this, callback, std::placeholders::_1));
}

void CommandRelay::Close() {
  fd_.Close();
}

}  // anonymous namespace

Relay::Handle NewUDPRelay(const Endpoint &endpoint) {
  auto relay = std::make_shared<UDPRelay>(endpoint);
  relay->Initialize();
  return relay;
}

Relay::Handle NewTCPRelay(const Endpoint &endpoint) {
  auto relay = std::make_shared<TCPRelay>(endpoint);
  relay->Initialize();
  return relay;
}

Relay::Handle NewCommandRelay(const std::string &cmd) {
  auto relay = std::make_shared<CommandRelay>(cmd);
  relay->Initialize();
  return relay;
}

}  // namespace pud
