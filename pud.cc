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
#include "util.h"

namespace pud {
namespace {
const char kDefaultStatePath[] = "/tmp/pud.state";

void Help(const ArgumentMap &args) {
  std::cout <<
      "PUD v2.0.0, built on " __DATE__ " " __TIME__ "\n"
      "Author: contem\n"
      "Usage: pud [COMMAND] [OPTIONS]...\n"
      "\n"
      "Network Commands:\n"
      "  new-network [master pub key] Starts a new network with the given\n"
      "                               master public key.\n"
      "  attach [endpoint]            Attaches to the network specified\n"
      "                               by another running peer.\n"
      "  load                         Loads an existing network from disk.\n"
      "\n"
      "Options:\n"
      "  --state-path [path]          Path to store network information.\n"
      "                               Defaults to " << kDefaultStatePath << "\n"
      "  --foreground                 Do not fork into the background.\n"
      "  --port [port]                Listen on the given port instead of a\n"
      "                               random port.\n"
      "\n";
}

std::unique_ptr<Peer> CreatePeer(const ArgumentMap &args) {
  return std::make_unique<Peer>(
      LocalFile(args.GetFlagWithDefault("state-path", kDefaultStatePath)));
}

void StartPeer(const ArgumentMap &args, std::unique_ptr<Peer> peer) {
  if (!args.HasFlag("foreground")) {
    std::cerr << ">>> Forking into the background" << std::endl;
    if (daemon(0, 1) < 0) {
      throw SystemError("Failed to fork into the background");
    }

    int32 dev_zero = open("/dev/zero", O_RDONLY | O_CLOEXEC);
    if (dev_zero < 0)
      throw SystemError("Failed to open /dev/zero");
    if (dup2(dev_zero, STDIN_FILENO) < 0)
      throw SystemError("Failed to duplicate fd");
    if (dev_zero != STDIN_FILENO)
      close(dev_zero);

    int32 dev_null = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (dev_null < 0)
      throw SystemError("Failed to open /dev/null");
    if (dup2(dev_null, STDOUT_FILENO) < 0)
      throw SystemError("Failed to duplicate fd");
    if (dup2(dev_null, STDERR_FILENO) < 0)
      throw SystemError("Failed to duplicate fd");
    if (dev_null != STDOUT_FILENO && dev_null != STDERR_FILENO)
      close(dev_null);
  }
  peer->Run();
}

void NewNetwork(const ArgumentMap &args) {
  const uint16 port =
      args.HasFlag("port")
          ? static_cast<uint16>(std::stoul(args.GetFlag("port")))
          : 0;
  std::unique_ptr<Peer> peer = CreatePeer(args);
  peer->NewNetwork(args.Arg(), port);
  StartPeer(args, std::move(peer));
}

void AttachToNetwork(const ArgumentMap &args) {
  const uint16 port =
      args.HasFlag("port")
          ? static_cast<uint16>(std::stoul(args.GetFlag("port")))
          : 0;
  std::unique_ptr<Peer> peer = CreatePeer(args);
  peer->AttachToNetwork(args.Arg(), port);
  StartPeer(args, std::move(peer));
}

void Load(const ArgumentMap &args) {
  std::unique_ptr<Peer> peer = CreatePeer(args);
  peer->LoadFromFile();
  StartPeer(args, std::move(peer));
}

void Start(int argc, char **argv) {
  CommandLineParser parser(argc, argv);
  parser.SetDefaultCommand("help");
  parser.AddCommand("help", &Help);
  parser.AddCommand("new-network", &NewNetwork, 1);
  parser.AddCommand("attach", &AttachToNetwork, 1);
  parser.AddCommand("load", &Load);
  parser.AddOption("state-path", true);
  parser.AddOptionAlias("p", "state-path");
  parser.AddOption("foreground");
  parser.AddOptionAlias("f", "foreground");
  parser.AddOption("port", true);
  parser.Parse();
}

}  // anonymous namespace
}  // namespace pud

int main(int argc, char **argv) {
  pud::Start(argc, argv);
  return 0;
}

