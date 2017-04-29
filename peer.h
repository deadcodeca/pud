// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "node.h"
#include "proto.h"
#include "relay.h"
#include "rsa.h"
#include "server.h"
#include "socket.h"
#include "util.h"

namespace pud {

enum Operation : uint8 {
  BOOTSTRAP = 0xb9,
  BOOTSTRAP_ACK = 0x90,
  GET_PEER_LIST = 0x2d,
  PEER_LIST = 0x4f,
  BROADCAST = 0x22,
  BROADCAST_ACK = 0xf3,
  RELAY_OPEN = 0xc8,
  RELAY_WRITE = 0x68,
  RELAY_CLOSE = 0xe0,
  RELAY_ACK = 0xf2,
  QUIT = 0xcc,
  NACK = 0xd6,
};

enum RelayType : uint8 {
  UDP_RELAY = 0x9c,
  TCP_RELAY = 0xf2,
  CMD_RELAY = 0x56,
};

class Peer {
 public:
  typedef std::function<std::pair<RSAPublic, RSAPrivate>()> KeyBuilderCallback;

  explicit Peer(File::Handle state_file, bool verbose = false);
  Peer(File::Handle state_file, KeyBuilderCallback key_builder_callback,
       bool verbose);

  void NewNetwork(const std::string &master_pubkey, uint16 local_port = 0);
  void AttachToNetwork(const std::string &endpoint_str, uint16 local_port = 0);
  void LoadFromFile();

  void Run();

  uint16 port() const { return server_ != nullptr ? server_->port() : 0; }
  uint64 peer_ident() const { return peer_ident_; }
  std::map<uint64, Node::Handle> nodes() const { return nodes_; }

  static void SendAndWaitForResponse(Server *server, const Endpoint &endpoint,
                                     const OutputBuffer &req,
                                     const std::set<Operation> &expected_op,
                                     int64 deadline_msec, InputBuffer *resp);

 private:
  struct BroadcastEntry {
    uint64 broadcast_id;
    std::set<uint64> sent_peer_ids;
    std::set<uint64> waiting_packet_ids;
    uint64 acks;
    bool sent;
    OutputBuffer data;

    BroadcastEntry() : broadcast_id(0), acks(0), sent(false) {}
  };

  struct RelayEntry {
    Endpoint source_endpoint;
    Relay::Handle relay;

    RelayEntry(const Endpoint &source_endpoint, Relay::Handle relay)
        : source_endpoint(source_endpoint), relay(std::move(relay)) {}
  };

  typedef std::list<BroadcastEntry> BroadcastList;
  typedef std::map<uint64, RelayEntry> RelayList;
  typedef std::map<Operation, void (Peer::*)(const Endpoint &, InputBuffer *)>
      OperationMap;

  void InitNewPeer(uint16 local_port = 0);
  void SyncWithNetwork(const Endpoint &endpoint, bool send_attach);
  void AddToBroadcast(std::vector<uint8>::const_iterator begin,
                      std::vector<uint8>::const_iterator end,
                      uint64 broadcast_id = 0);
  void SaveToFile();

  void SendNodeUpdate();
  BroadcastList::iterator SendBroadcast(BroadcastList::iterator entry);
  void MaintenanceCycle();

  void ReadFromServer(const Endpoint &read_endpoint,
                      const std::string &read_buf);
  void ReadFromRelay(RelayList::iterator entry, Relay::Control control,
                     const std::string &buf);

  void BootstrapOp(const Endpoint &endpoint, InputBuffer *resp);
  void GetPeerListOp(const Endpoint &endpoint, InputBuffer *resp);
  void BroadcastOp(const Endpoint &endpoint, InputBuffer *resp);
  void BroadcastAckOp(const Endpoint &endpoint, InputBuffer *resp);
  void RelayOpenOp(const Endpoint &endpoint, InputBuffer *resp);
  void RelayWriteOp(const Endpoint &endpoint, InputBuffer *resp);
  void RelayCloseOp(const Endpoint &endpoint, InputBuffer *resp);
  void QuitOp(const Endpoint &endpoint, InputBuffer *resp);

  const File::Handle state_file_;
  const bool verbose_;

  KeyBuilderCallback key_builder_callback_;
  bool peer_registered_;
  uint64 peer_ident_;
  std::unique_ptr<RSAPublic> master_pubkey_;
  std::unique_ptr<RSAPublic> peer_pubkey_;
  std::unique_ptr<RSAPrivate> peer_privkey_;
  std::unique_ptr<Server> server_;
  std::map<uint64, Node::Handle> nodes_;
  BroadcastList broadcasts_;
  RelayList relays_;
  bool shutdown_;
  std::chrono::steady_clock::time_point last_node_update_;
  std::chrono::steady_clock::time_point last_broadcast_;

  static const OperationMap operation_map_;
};

}  // namespace pud
