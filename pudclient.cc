// Copyright (C) 2016, All rights reserved.
// Author: contem

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include "exception.h"
#include "getopt.h"
#include "peer.h"
#include "rsa.h"
#include "server.h"
#include "util.h"

namespace pud {
namespace {
const uint16 kClientPortLow = 16384;
const uint16 kClientPortHigh = 65535;
const int64 kClientDeadlineMsec = 15000;
const int64 kMasterKeySize = 2048;
const char kDefaultStatePath[] = "/tmp/pudclient.state";
const char kListHeaderFormat[] = "%-16s %-21s %-16s";
const char kListEntryFormat[] = "%16lx %-21s %-16lu";

void Help(const ArgumentMap &args) {
  std::cout <<
      "PUD Client v2.0.0, built on " __DATE__ " " __TIME__ "\n"
      "Author: contem\n"
      "Usage: pudclient [COMMAND] [OPTIONS]...\n"
      "\n"
      "Network Commands:\n"
      "  create-network               Generates a new master public key\n"
      "                               to create a new network.\n"
      "  sync [endpoint]              Fetches the list of peers from the\n"
      "                               given endpoint.\n"
      "\n"
      "Control Commands:\n"
      "  list                         Prints the list of peers. You must run\n"
      "                               the 'sync' command first.\n"
      "  command [endpoint] [args..]  Runs a command at the given peer.\n"
      "\n"
      "Options:\n"
      "  --state-path [path]          Path to store network information.\n"
      "                               Defaults to " << kDefaultStatePath << "\n"
      "\n";
}

File::Handle GetStateFile(const ArgumentMap &args) {
  return LocalFile(args.GetFlagWithDefault("state-path", kDefaultStatePath));
}

std::shared_ptr<Server> NewServer() {
  std::uniform_int_distribution<uint16> dist(kClientPortLow, kClientPortHigh);
  std::shared_ptr<Server> server(new Server(dist(LocalRNG())));
  try {
    server->Listen();
  } catch (const SystemError &e) {
    std::cerr << "Failed to listen on port " << server->port()
              << ", trying another..." << std::endl;
    _exit(-1);
  }
  return server;
}

void CreateNetwork(const ArgumentMap &args) {
  std::cout << ">>> Generating a new master key, this may take a few minutes..."
            << std::endl;
  std::pair<RSAPublic, RSAPrivate> p = MakeRSAKey(kMasterKeySize, true);
  OutputBuffer pubbuf;
  WriteRSAKey(p.first, &pubbuf);

  std::cout << std::endl;
  std::cout << "Master public key:" << std::endl;
  std::cout << "   " << Base64Encode(pubbuf.ToString()) << std::endl;

  OutputBuffer outfile;
  WriteRSAKey(p.first, &outfile);
  WriteRSAKey(p.second, &outfile);
  GetStateFile(args)->Write(outfile.ToString());

  std::cout << std::endl;
  std::cout << "The master public / private key has been written to the "
               "state file." << std::endl;
  std::cout << std::endl;
  std::cout << "To start a new network with the new master public key, run the "
               "following:" << std::endl;
  std::cout << "   pud new-network [...master public key...]" << std::endl;
}

void Sync(const ArgumentMap &args) {
  File::Handle state_file = GetStateFile(args);
  InputBuffer infile(state_file->Read());
  const RSAPublic master_pubkey = ReadRSAKey<RSAPublic>(&infile);
  const RSAPrivate master_privkey = ReadRSAKey<RSAPrivate>(&infile);
  const Endpoint endpoint(args.Arg(0));
  std::shared_ptr<Server> server = NewServer();
  std::map<uint64, Node::Handle> nodes;
  uint64 offset = 0;
  std::cout << ">>> Fetching peer list..." << std::flush;
  for (;;) {
    OutputBuffer req;
    req.PushU8(GET_PEER_LIST);
    req.PushU64(offset);

    InputBuffer resp;
    Peer::SendAndWaitForResponse(server.get(), endpoint, req, {PEER_LIST, NACK},
                                 kClientDeadlineMsec, &resp);
    const Operation op = static_cast<Operation>(resp.PopU8());
    if (op == NACK)
      throw InternalError("Failed to fetch list of peers");
    const uint64 size = resp.PopU64();
    const uint64 recv_offset = resp.PopU64();
    if (recv_offset != offset)
      continue;
    while (!resp.empty()) {
      const Node node = ReadNode(&resp);
      nodes[node.ident()] = std::make_shared<Node>(node);
      offset++;
    }
    if (offset >= size)
      break;
    std::cout << ">>> Fetching peer list (" << offset << "/" << size << ")..."
              << std::flush;
  }
  std::cout << "Done" << std::endl;

  OutputBuffer outfile;
  WriteRSAKey(master_pubkey, &outfile);
  WriteRSAKey(master_privkey, &outfile);
  for (const auto &node : nodes)
    WriteNode(*node.second, &outfile);
  state_file->Write(outfile.ToString());
}

void List(const ArgumentMap &args) {
  File::Handle state_file = GetStateFile(args);
  InputBuffer infile(state_file->Read());
  const RSAPublic master_pubkey = ReadRSAKey<RSAPublic>(&infile);
  const RSAPrivate master_privkey = ReadRSAKey<RSAPrivate>(&infile);
  std::cout << Format(kListHeaderFormat, "Ident", "Endpoint", "Last Ping (s)")
            << std::endl;
  std::cout << Format(kListHeaderFormat, "-----", "--------", "-------------")
            << std::endl;
  int32 count = 0;
  while (!infile.empty()) {
    const Node node = ReadNode(&infile);
    std::cout << Format(kListEntryFormat, node.ident(),
                        node.endpoint().ToString().c_str(),
                        node.last_seen_ago())
              << std::endl;
    count++;
  }
  if (count == 0) {
    std::cout << "No peers found, try running the 'sync' command first."
              << std::endl;
  }
}

enum class RelayState { PENDING, OPENED, CLOSED };

void CommandRead(RelayState *state, uint64 expected_relay_id,
                 const Endpoint &read_endpoint, const std::string &read_buf) {
  InputBuffer resp(read_buf);
  const Operation op = static_cast<Operation>(resp.PopU8());
  const uint64 relay_id = resp.PopU64();
  if (relay_id != expected_relay_id)
    return;
  switch (op) {
    case RELAY_WRITE: {
      const size_t r_len = resp.PopVariableLength();
      const std::string msg = ByteStringToString(resp.Pop(r_len));
      (void)write(STDOUT_FILENO, msg.data(), msg.size());
    } break;

    case RELAY_CLOSE: {
      const size_t r_len = resp.PopVariableLength();
      if (r_len) {
        const std::string error = ByteStringToString(resp.Pop(r_len));
        std::cout << ">>> Connection closed: " << error << std::endl;
      } else {
        std::cout << ">>> Connection closed" << std::endl;
      }
      *state = RelayState::CLOSED;
    } break;

    case RELAY_ACK:
      if (*state == RelayState::PENDING) {
        std::cout << ">>> Relay successfully opened" << std::endl;
        *state = RelayState::OPENED;
      }
      break;

    case NACK: {
      const size_t r_len = resp.PopVariableLength();
      const std::string error = ByteStringToString(resp.Pop(r_len));
      std::cout << ">>> Relay failed: " << error << std::endl;
      *state = RelayState::CLOSED;
    } break;

    default:
      break;
  }
}

void Command(const ArgumentMap &args) {
  File::Handle state_file = GetStateFile(args);
  InputBuffer infile(state_file->Read());
  const RSAPublic master_pubkey = ReadRSAKey<RSAPublic>(&infile);
  const RSAPrivate master_privkey = ReadRSAKey<RSAPrivate>(&infile);
  const Endpoint endpoint(args.Arg(0));
  std::string cmd;
  for (size_t i = 1; i < args.Args().size(); i++)
    cmd.append(args.Arg(i));
  const uint64 relay_id =
        std::uniform_int_distribution<uint64>(1)(LocalRNG());
  OutputBuffer buf;
  buf.PushU8(CMD_RELAY);
  buf.PushU64(relay_id);
  buf.PushVariableLength(cmd.size());
  buf.Push(StringToByteString(cmd));
  WriteSignature(master_privkey, Hash(buf.data()), &buf);
  OutputBuffer req;
  req.PushU8(RELAY_OPEN);
  req.Push(buf.data());
  std::shared_ptr<Server> server = NewServer();
  server->Send(endpoint, req.ToString());
  std::cout << ">>> Sending command to peer..." << std::endl;
  const std::chrono::steady_clock::time_point abs_deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(kClientDeadlineMsec);
  RelayState state = RelayState::PENDING;
  for (;;) {
    if (state == RelayState::PENDING &&
        std::chrono::steady_clock::now() >= abs_deadline) {
      throw UnknownError("Deadline exceeded while contacting peer");
    }
    const int64 timeout_msec =
        state != RelayState::PENDING
            ? kClientDeadlineMsec
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  abs_deadline - std::chrono::steady_clock::now()).count();
    const std::vector<Pollable> fds{server->ReadEvent(
        std::bind(&CommandRead, &state, relay_id, std::placeholders::_1,
                  std::placeholders::_2))};
    Poll(fds, timeout_msec);
    if (state == RelayState::CLOSED)
      break;
  }
}

void Start(int argc, char **argv) {
  CommandLineParser parser(argc, argv);
  parser.SetDefaultCommand("help");
  parser.AddCommand("help", &Help);
  parser.AddCommand("create-network", &CreateNetwork);
  parser.AddCommand("sync", &Sync, 1);
  parser.AddCommand("list", &List);
  parser.AddCommand("command", &Command, 2);
  parser.AddCommand("run", &Command, 2);
  parser.AddOption("state-path", true);
  parser.AddOptionAlias("p", "state-path");
  parser.Parse();
}

}  // anonymous namespace
}  // namespace pud

int main(int argc, char **argv) {
  pud::Start(argc, argv);
  return 0;
}

