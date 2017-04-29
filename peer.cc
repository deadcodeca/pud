// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "peer.h"

#include <assert.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "exception.h"
#include "proto.h"
#include "rsa.h"
#include "sha256.h"
#include "util.h"

namespace pud {
namespace {

const uint64 kPeerKeyBitSize = 512;
const uint16 kPeerPortLow = 16384;
const uint16 kPeerPortHigh = 65535;
const int64 kAttachDeadlineMsec = 15000;
const int64 kPacketRetryIntervalMsec = 1000;
const int64 kMaintenanceCycleIntervalMsec = 1000;
const int64 kSendNodeUpdateIntervalMsec = 120000;
const int64 kSendBroadcastIntervalMsec = 3000;
const uint64 kBroadcastAckCount = 2;
const uint64 kNodeAliveLastSeenSec = 600;

class SortPeerForBroadcast {
 public:
  SortPeerForBroadcast(uint64 broadcast_id, uint64 peer_ident)
      : broadcast_id_(broadcast_id), peer_ident_(peer_ident ^ broadcast_id) {}
  bool operator()(uint64 a, uint64 b) { return Translate(a) < Translate(b); }

 private:
  uint64 Translate(uint64 n) {
    n ^= broadcast_id_;
    n -= peer_ident_;
    return n;
  }

  const uint64 broadcast_id_;
  const uint64 peer_ident_;
};

}  // anonymous namespace

// static
const Peer::OperationMap Peer::operation_map_{
    {BOOTSTRAP, &Peer::BootstrapOp},
    {GET_PEER_LIST, &Peer::GetPeerListOp},
    {BROADCAST, &Peer::BroadcastOp},
    {BROADCAST_ACK, &Peer::BroadcastAckOp},
    {RELAY_OPEN, &Peer::RelayOpenOp},
    {RELAY_WRITE, &Peer::RelayWriteOp},
    {RELAY_CLOSE, &Peer::RelayCloseOp},
    {QUIT, &Peer::QuitOp},
};

Peer::Peer(File::Handle state_file, bool verbose)
    : Peer(std::move(state_file), std::bind(&MakeRSAKey, kPeerKeyBitSize, true),
           verbose) {
}

Peer::Peer(File::Handle state_file, KeyBuilderCallback key_builder_callback,
           bool verbose)
    : state_file_(state_file),
      verbose_(verbose),
      key_builder_callback_(key_builder_callback),
      peer_registered_(false),
      peer_ident_(0),
      shutdown_(false) {
}

void Peer::NewNetwork(const std::string &master_pubkey, uint16 local_port) {
  assert(!peer_registered_);
  InputBuffer p(Base64Decode(master_pubkey));
  try {
    master_pubkey_.reset(new RSAPublic(ReadRSAKey<RSAPublic>(&p)));
  } catch (const Exception &e) {
    throw InternalError("Failed to parse master public key");
  }
  if (!p.empty())
    throw InternalError("Extraneous bytes at end of master public key");

  InitNewPeer(local_port);
  SaveToFile();
}

void Peer::AttachToNetwork(const std::string &endpoint_str, uint16 local_port) {
  assert(!peer_registered_);
  InitNewPeer(local_port);
  SyncWithNetwork(Endpoint(endpoint_str), true);
  SaveToFile();
}

void Peer::SyncWithNetwork(const Endpoint &endpoint, bool send_attach) {
  if (send_attach) {
    std::cout << ">>> Attempting to attach to network " << endpoint
              << std::endl;

    OutputBuffer req;
    req.PushU8(BOOTSTRAP);
    req.PushU32(endpoint.address());

    InputBuffer resp;
    SendAndWaitForResponse(server_.get(), endpoint, req, {BOOTSTRAP_ACK, NACK},
                           kAttachDeadlineMsec, &resp);
    const Operation op = static_cast<Operation>(resp.PopU8());
    if (op == NACK)
      throw InternalError("Failed to bootstrap peer");
    const in_addr_t my_address = resp.PopU32();
    if (my_address == INADDR_NONE)
      throw InternalError("Invalid endpoint address for attach");
    master_pubkey_.reset(new RSAPublic(ReadRSAKey<RSAPublic>(&resp)));
    nodes_[peer_ident_] = std::make_shared<Node>(
        Endpoint(my_address, server_->port()), *peer_pubkey_, peer_ident_, 0);
    peer_registered_ = true;
    SaveToFile();
  }

  std::cout << ">>> Fetching peer list" << std::endl;
  uint64 offset = 0;
  for (;;) {
    OutputBuffer req;
    req.PushU8(GET_PEER_LIST);
    req.PushU64(offset);

    InputBuffer resp;
    SendAndWaitForResponse(server_.get(), endpoint, req, {PEER_LIST, NACK},
                           kAttachDeadlineMsec, &resp);
    const Operation op = static_cast<Operation>(resp.PopU8());
    if (op == NACK)
      throw InternalError("Failed to fetch peer list");
    const uint64 size = resp.PopU64();
    const uint64 recv_offset = resp.PopU64();
    if (recv_offset != offset)
      continue;
    while (!resp.empty()) {
      const Node node = ReadNode(&resp);
      const auto it = nodes_.find(node.ident());
      if (it == nodes_.end() ||
          node.last_known_sequence() > it->second->last_known_sequence()) {
        nodes_[node.ident()] = std::make_shared<Node>(node);
      }
      offset++;
    }
    if (offset >= size)
      break;
    std::cout << ">>> Fetching peer list (" << offset << "/" << size << ")"
              << std::endl;
  }

  std::cout << ">>> Attached to network" << std::endl;
}

void Peer::AddToBroadcast(std::vector<uint8>::const_iterator begin,
                          std::vector<uint8>::const_iterator end,
                          uint64 broadcast_id) {
  const size_t size = static_cast<size_t>(std::distance(begin, end));
  bool existing_broadcast_id = false;
  for (BroadcastEntry &entry : broadcasts_) {
    if (broadcast_id != 0 && entry.broadcast_id == broadcast_id)
      existing_broadcast_id = true;
    if (!entry.sent && entry.data.size() + size < Server::kMaxPacketSize &&
        (broadcast_id == 0 || entry.broadcast_id == broadcast_id)) {
      entry.data.Push(begin, end);
      return;
    }
  }
  broadcasts_.emplace_back();
  BroadcastEntry &entry = broadcasts_.back();
  entry.broadcast_id = existing_broadcast_id ? 0 : broadcast_id;
  entry.data.Push(begin, end);
}

// static
void Peer::SendAndWaitForResponse(Server *server, const Endpoint &endpoint,
                                  const OutputBuffer &req,
                                  const std::set<Operation> &expected_op,
                                  int64 deadline_msec, InputBuffer *resp) {
  const std::chrono::steady_clock::time_point abs_deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(deadline_msec);
  std::chrono::steady_clock::time_point next_send;
  for (;;) {
    if (std::chrono::steady_clock::now() >= next_send) {
      server->Send(endpoint, req.ToString());
      next_send = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kPacketRetryIntervalMsec);
    }
    if (std::chrono::steady_clock::now() >= abs_deadline)
      throw UnknownError("Deadline exceeded while contacting peer");
    const int64 timeout_msec = std::max(
        1L,
        std::min(std::chrono::duration_cast<std::chrono::milliseconds>(
                     abs_deadline - std::chrono::steady_clock::now()).count(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     next_send - std::chrono::steady_clock::now()).count()));
    bool got_packet = false;
    const std::vector<Pollable> fds{server->ReadEvent(
        [&](const Endpoint &read_endpoint, const std::string &read_buf) {
          if (read_endpoint != endpoint)
            return;
          InputBuffer buf(read_buf);
          const Operation op = static_cast<Operation>(buf.PopU8());
          if (expected_op.find(op) != expected_op.end()) {
            *resp = InputBuffer(read_buf);
            got_packet = true;
          }
        })};
    Poll(fds, timeout_msec);
    if (got_packet)
      return;
  }
}

void Peer::InitNewPeer(uint16 local_port) {
  peer_ident_ = std::uniform_int_distribution<uint64>(1)(LocalRNG());

  std::cout << ">>> Building peer public key" << std::endl;
  std::pair<RSAPublic, RSAPrivate> p = key_builder_callback_();
  peer_pubkey_.reset(new RSAPublic(p.first));
  peer_privkey_.reset(new RSAPrivate(p.second));

  std::uniform_int_distribution<uint16> dist(kPeerPortLow, kPeerPortHigh);
  for (;;) {
    const uint16 udp_port =
        local_port != 0 ? local_port : dist(LocalRNG());
    server_.reset(new Server(udp_port));
    try {
      server_->Listen();
      break;
    } catch (const SystemError &e) {
      std::cerr << "Failed to listen on port " << server_->port()
                << ", trying another..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  std::cout << ">>> Listening on port " << server_->port() << std::endl;
}

void Peer::LoadFromFile() {
  assert(!peer_registered_);
  std::cout << ">>> Loading state from file " << std::endl;
  InputBuffer resp(state_file_->Read());
  peer_ident_ = resp.PopU64();
  server_.reset(new Server(resp.PopU16()));
  server_->Listen();
  std::cout << ">>> Listening on port " << server_->port() << std::endl;
  master_pubkey_.reset(new RSAPublic(ReadRSAKey<RSAPublic>(&resp)));
  peer_pubkey_.reset(new RSAPublic(ReadRSAKey<RSAPublic>(&resp)));
  peer_privkey_.reset(new RSAPrivate(ReadRSAKey<RSAPrivate>(&resp)));
  while (!resp.empty()) {
    const Node node = ReadNode(&resp);
    nodes_[node.ident()] = std::make_shared<Node>(node);
  }

  if (nodes_.find(peer_ident_) == nodes_.end())
    throw InternalError("Invalid information in state file");
  peer_registered_ = true;
}

void Peer::SaveToFile() {
  OutputBuffer req;
  req.PushU64(peer_ident_);
  req.PushU16(server_->port());
  WriteRSAKey(*master_pubkey_, &req);
  WriteRSAKey(*peer_pubkey_, &req);
  WriteRSAKey(*peer_privkey_, &req);
  for (const auto &node : nodes_)
    WriteNode(*node.second, &req);
  state_file_->Write(req.ToString());
}

void Peer::SendNodeUpdate() {
  assert(peer_registered_);
  const Node::Handle &self = nodes_[peer_ident_];
  self->set_last_known_sequence(self->last_known_sequence() + 1);
  self->set_last_seen();
  SaveToFile();

  OutputBuffer req;
  req.PushU32(self->endpoint().address());
  req.PushU16(server_->port());
  req.PushU64(peer_ident_);
  req.PushU64(self->last_known_sequence());
  WriteRSAKey(*peer_pubkey_, &req);
  WriteSignature(*peer_privkey_, Hash(req.data()), &req);
  const std::vector<uint8> &data = req.data();
  AddToBroadcast(data.cbegin(), data.cend());
}

Peer::BroadcastList::iterator Peer::SendBroadcast(
    BroadcastList::iterator entry) {
  if (entry->broadcast_id == 0) {
    entry->broadcast_id =
        std::uniform_int_distribution<uint64>(1)(LocalRNG());
  }
  if (entry->acks >= kBroadcastAckCount) {
    return broadcasts_.erase(entry);
  }
  std::map<uint64, Node::Handle, SortPeerForBroadcast> peers{
      SortPeerForBroadcast(entry->broadcast_id, peer_ident_)};
  peers.insert(nodes_.begin(), nodes_.end());
  for (int32 pass = 0; pass < 2; pass++) {
    const bool relaxed = pass == 1;
    for (const auto &peer : peers) {
      if (peer.first == peer_ident_ ||
          (!relaxed && peer.second->last_seen_ago() > kNodeAliveLastSeenSec) ||
          entry->sent_peer_ids.find(peer.first) != entry->sent_peer_ids.end()) {
        continue;
      }
      const uint64 packet_id =
          std::uniform_int_distribution<uint64>()(LocalRNG());
      OutputBuffer req;
      req.PushU8(BROADCAST);
      req.PushU64(entry->broadcast_id);
      req.PushU64(packet_id);
      req.Push(entry->data.data());
      server_->Send(peer.second->endpoint(), req.ToString());
      entry->sent_peer_ids.emplace(peer.first);
      entry->waiting_packet_ids.emplace(packet_id);
      entry->sent = true;
      return std::next(entry);
    }
  }
  return broadcasts_.erase(entry);
}

void Peer::MaintenanceCycle() {
  if (!peer_registered_)
    return;
  if (std::chrono::steady_clock::now() >=
      last_node_update_ +
          std::chrono::milliseconds(kSendNodeUpdateIntervalMsec)) {
    SendNodeUpdate();
    last_node_update_ = std::chrono::steady_clock::now();
  }
  if (broadcasts_.size() > 1 ||
      std::chrono::steady_clock::now() >=
          last_broadcast_ +
              std::chrono::milliseconds(kSendBroadcastIntervalMsec)) {
    for (auto it = broadcasts_.begin(); it != broadcasts_.end();)
      it = SendBroadcast(it);
    last_broadcast_ = std::chrono::steady_clock::now();
  }
}

void Peer::ReadFromServer(const Endpoint &read_endpoint,
                          const std::string &read_buf) {
  try {
    InputBuffer resp(read_buf);
    const Operation op = static_cast<Operation>(resp.PopU8());
    const auto it = operation_map_.find(op);
    if (it != operation_map_.end()) {
      (this->*it->second)(read_endpoint, &resp);
    } else if (verbose_) {
      throw InvalidArgument(Concat("Invalid operation type ", op));
    }
  } catch (const Exception &e) {
    if (verbose_) {
      std::cerr << "[#] Exception was thrown while handling packet: "
                << e.what() << std::endl;
    }
    const std::string what = e.what();
    OutputBuffer req;
    req.PushU8(NACK);
    req.PushVariableLength(what.size());
    req.Push(StringToByteString(what));
    server_->Send(read_endpoint, req.ToString());
  }
}

void Peer::ReadFromRelay(RelayList::iterator entry, Relay::Control control,
                         const std::string &buf) {
  OutputBuffer req;
  switch (control) {
    case Relay::Control::OPEN:
      req.PushU8(RELAY_OPEN);
      req.PushU64(entry->first);
      server_->Send(entry->second.source_endpoint, req.ToString());
      break;

    case Relay::Control::WRITE:
      req.PushU8(RELAY_WRITE);
      req.PushU64(entry->first);
      req.PushVariableLength(buf.size());
      req.Push(StringToByteString(buf));
      server_->Send(entry->second.source_endpoint, req.ToString());
      break;

    case Relay::Control::CLOSE:
      req.PushU8(RELAY_CLOSE);
      req.PushU64(entry->first);
      req.PushVariableLength(buf.size());
      req.Push(StringToByteString(buf));
      server_->Send(entry->second.source_endpoint, req.ToString());
      relays_.erase(entry);
      break;
  }
}

void Peer::Run() {
  assert(peer_ident_ != 0);
  shutdown_ = false;
  std::chrono::steady_clock::time_point next_update;
  while (!shutdown_) {
    if (std::chrono::steady_clock::now() >= next_update) {
      MaintenanceCycle();
      next_update = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kMaintenanceCycleIntervalMsec);
    }
    const int64 timeout_msec = std::max(
        1L, std::chrono::duration_cast<std::chrono::milliseconds>(
                next_update - std::chrono::steady_clock::now()).count());
    std::vector<Pollable> fds{server_->ReadEvent(
        std::bind(&Peer::ReadFromServer, this, std::placeholders::_1,
                  std::placeholders::_2))};
    for (auto it = relays_.begin(); it != relays_.end(); ++it) {
      fds.emplace_back(it->second.relay->ReadEvent(
          std::bind(&Peer::ReadFromRelay, this, it, std::placeholders::_1,
                    std::placeholders::_2)));
    }
    try {
      Poll(fds, timeout_msec >= 0 ? timeout_msec : 0);
    } catch (const Exception &e) {
      if (verbose_) {
        std::cerr << "[#] Exception was thrown while handling poll: "
                  << e.what() << std::endl;
      }
    }
  }
}

void Peer::BootstrapOp(const Endpoint &endpoint, InputBuffer *resp) {
  const in_addr_t my_address = resp->PopU32();
  if (!peer_registered_) {
    if (my_address == INADDR_NONE)
      throw InternalError("Invalid endpoint address for attach");
    nodes_[peer_ident_] = std::make_shared<Node>(
        Endpoint(my_address, server_->port()), *peer_pubkey_, peer_ident_, 0);
    peer_registered_ = true;
    SaveToFile();
  }

  OutputBuffer req;
  req.PushU8(BOOTSTRAP_ACK);
  req.PushU32(endpoint.address());
  WriteRSAKey(*master_pubkey_, &req);
  server_->Send(endpoint, req.ToString());
}

void Peer::GetPeerListOp(const Endpoint &endpoint, InputBuffer *resp) {
  const uint64 offset = resp->PopU64();

  OutputBuffer req;
  req.PushU8(PEER_LIST);
  req.PushU64(nodes_.size());
  req.PushU64(offset);
  if (offset < nodes_.size()) {
    auto it = nodes_.cbegin();
    std::advance(it, offset);
    for (; it != nodes_.cend(); ++it) {
      OutputBuffer tmp_buf;
      WriteNode(*it->second, &tmp_buf);
      if (req.size() + tmp_buf.size() > Server::kMaxPacketSize)
        break;
      req.Push(tmp_buf.data());
    }
  }
  server_->Send(endpoint, req.ToString());
}

void Peer::BroadcastOp(const Endpoint &endpoint, InputBuffer *resp) {
  if (!peer_registered_)
    return;
  const uint64 broadcast_id = resp->PopU64();
  const uint64 packet_id = resp->PopU64();
  bool updated = false;
  while (!resp->empty()) {
    const std::vector<uint8>::const_iterator start_ptr = resp->ptr();
    const in_addr_t address = resp->PopU32();
    const uint16 port = resp->PopU16();
    const uint64 ident = resp->PopU64();
    const uint64 seq = resp->PopU64();
    const RSAPublic key = ReadRSAKey<RSAPublic>(resp);
    if (!VerifySignature(key, Hash(start_ptr, resp->ptr()), resp))
      throw ObjectAlreadyExists("Signature verification failed");

    const Endpoint node_endpoint(address, port);
    const auto it = nodes_.find(ident);
    if (it != nodes_.end()) {
      Node::Handle node = it->second;
      if (node->last_known_sequence() >= seq)
        continue;
      if (node->key() != key)
        throw InvalidArgument("Public key mismatch");
      if (node->endpoint() != node_endpoint)
        node->set_endpoint(node_endpoint);
      node->set_last_known_sequence(seq);
      node->set_last_seen();
    } else {
      nodes_[ident] = std::make_shared<Node>(node_endpoint, key, ident, seq);
    }
    AddToBroadcast(start_ptr, resp->ptr(), broadcast_id);
    updated = true;
  }
  if (updated)
    SaveToFile();

  OutputBuffer req;
  req.PushU8(BROADCAST_ACK);
  req.PushU64(broadcast_id);
  req.PushU64(packet_id);
  server_->Send(endpoint, req.ToString());
}

void Peer::BroadcastAckOp(const Endpoint &endpoint, InputBuffer *resp) {
  const uint64 broadcast_id = resp->PopU64();
  const uint64 packet_id = resp->PopU64();
  for (BroadcastEntry &entry : broadcasts_) {
    if (entry.broadcast_id == broadcast_id && entry.sent) {
      auto it = entry.waiting_packet_ids.find(packet_id);
      if (it != entry.waiting_packet_ids.end()) {
        entry.waiting_packet_ids.erase(it);
        entry.acks++;
      }
      return;
    }
  }
  throw InvalidArgument("Invalid broadcast ID");
}

void Peer::RelayOpenOp(const Endpoint &endpoint, InputBuffer *resp) {
  const std::vector<uint8>::const_iterator start_ptr = resp->ptr();
  const uint8 relay_type = resp->PopU8();
  const uint64 relay_id = resp->PopU64();
  if (relays_.find(relay_id) != relays_.end())
    throw InternalError("Existing relay already opened");
  switch (relay_type) {
    case UDP_RELAY:
    case TCP_RELAY: {
      const in_addr_t address = resp->PopU32();
      if (address == INADDR_NONE)
        throw InternalError("Invalid endpoint address for relay");
      const uint16 port = resp->PopU16();
      if (!VerifySignature(*master_pubkey_, Hash(start_ptr, resp->ptr()), resp))
        throw InternalError("Signature verification failed");
      const Endpoint relay_endpoint(address, port);
      if (relay_type == UDP_RELAY) {
        relays_.emplace(relay_id,
                        RelayEntry(endpoint, NewUDPRelay(relay_endpoint)));
      } else {
        relays_.emplace(relay_id,
                        RelayEntry(endpoint, NewTCPRelay(relay_endpoint)));
      }
    } break;

    case CMD_RELAY: {
      const size_t r_len = resp->PopVariableLength();
      const std::string cmd = ByteStringToString(resp->Pop(r_len));
      if (!VerifySignature(*master_pubkey_, Hash(start_ptr, resp->ptr()), resp))
        throw InternalError("Signature verification failed");
      relays_.emplace(relay_id, RelayEntry(endpoint, NewCommandRelay(cmd)));
    } break;

    default:
      throw InvalidArgument("Invalid relay type");
  }

  OutputBuffer req;
  req.PushU8(RELAY_ACK);
  req.PushU64(relay_id);
  server_->Send(endpoint, req.ToString());
}

void Peer::RelayWriteOp(const Endpoint &endpoint, InputBuffer *resp) {
  const std::vector<uint8>::const_iterator start_ptr = resp->ptr();
  const uint64 relay_id = resp->PopU64();
  const auto it = relays_.find(relay_id);
  if (it == relays_.end())
    throw InternalError("Failed to find relay with the given ID");
  const size_t r_len = resp->PopVariableLength();
  const std::string cmd = ByteStringToString(resp->Pop(r_len));
  if (!VerifySignature(*master_pubkey_, Hash(start_ptr, resp->ptr()), resp))
    throw InternalError("Signature verification failed");
  it->second.relay->Send(cmd);
}

void Peer::RelayCloseOp(const Endpoint &endpoint, InputBuffer *resp) {
  const std::vector<uint8>::const_iterator start_ptr = resp->ptr();
  const uint64 relay_id = resp->PopU64();
  const auto it = relays_.find(relay_id);
  if (it == relays_.end())
    throw InternalError("Failed to find relay with the given ID");
  if (!VerifySignature(*master_pubkey_, Hash(start_ptr, resp->ptr()), resp))
    throw InternalError("Signature verification failed");
  it->second.relay->Close();
  relays_.erase(it);
}

void Peer::QuitOp(const Endpoint &endpoint, InputBuffer *resp) {
  OutputBuffer hash_buf;
  hash_buf.PushU64(peer_ident_);
  if (VerifySignature(*master_pubkey_, Hash(hash_buf.data()), resp))
    shutdown_ = true;
}

}  // namespace pud
