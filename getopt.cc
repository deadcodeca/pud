// Copyright (C) 2016, All rights reserved.
// Author: contem

#include "getopt.h"

#include <assert.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <map>
#include "exception.h"
#include "util.h"

namespace pud {

void Execute::Call(const char *command_name, const ArgumentMap &args) const {
  if (args.Count() < min_arg_count_) {
    throw InvalidArgument(
        Concat("Not enough arguments for command ", command_name));
  }
  callback_(args);
}

void Flag::Call(const char *option_name, const char *option_value,
                ArgumentMap *args) const {
  if (want_arg_)
    args->SetFlag(option_name, option_value);
  else
    args->SetFlag(option_name, "true");
}

void CommandLineParser::SetDefaultCommand(const std::string &command_name) {
  default_command_name_ = command_name;
}

void CommandLineParser::AddCommand(const std::string &command_name,
                                   const Command &command) {
  commands_[command_name] = command.Clone();
}

void CommandLineParser::AddCommand(const std::string &command_name,
                                   Execute::Callback callback,
                                   size_t min_arg_count) {
  commands_[command_name] =
      std::make_unique<Execute>(std::move(callback), min_arg_count);
}

void CommandLineParser::AddOption(const std::string &option_name,
                                  const Option &option) {
  options_[option_name] = option.Clone();
}

void CommandLineParser::AddOption(const std::string &option_name,
                                  bool want_arg) {
  options_[option_name] = std::make_unique<Flag>(want_arg);
}

void CommandLineParser::AddOptionAlias(const std::string &option_name,
                                       const std::string &alias) {
  option_alias_[option_name] = alias;
}

void CommandLineParser::Parse() {
  struct option opt_field[options_.size() + option_alias_.size() + 1];
  struct option *opt_ptr = opt_field;
  for (const auto &item : options_) {
    opt_ptr->name = item.first.c_str();
    opt_ptr->has_arg = item.second->WantArgument();
    opt_ptr->flag = nullptr;
    opt_ptr->val = 0;
    opt_ptr++;
  }
  for (const auto &item : option_alias_) {
    auto option_item = options_.find(item.second);
    assert(option_item != options_.end());
    opt_ptr->name = item.first.c_str();
    opt_ptr->has_arg = option_item->second->WantArgument();
    opt_ptr->flag = nullptr;
    opt_ptr->val = 0;
    opt_ptr++;
  }
  opt_ptr->name = nullptr;

  ArgumentMap args;
  for (;;) {
    int index;
    const int ret = getopt_long_only(argc_, argv_, "", opt_field, &index);
    if (ret == '?')
      exit(-1);
    if (ret < 0)
      break;
    assert(index >= 0);
    const char *option_name = opt_field[index].name;
    auto option_item = options_.find(option_name);
    if (option_item == options_.end()) {
      auto option_alias_item = option_alias_.find(option_name);
      assert(option_alias_item != option_alias_.end());
      option_name = option_alias_item->second.c_str();
      option_item = options_.find(option_name);
    }
    assert(option_item != options_.end());
    option_item->second->Call(
        option_name, opt_field[index].has_arg ? optarg : nullptr, &args);
  }

  const char *command_name = nullptr;
  if (optind >= argc_ || strlen(argv_[optind]) == 0) {
    if (default_command_name_.empty()) {
      std::cerr << "No command specified on command line and default command "
                   "was not specified for this application." << std::endl;
      exit(-1);
    }
    command_name = default_command_name_.c_str();
  } else {
    command_name = argv_[optind++];
  }
  auto command_item = commands_.find(command_name);
  if (command_item == commands_.end()) {
    std::cerr << "Unknown or invalid command " << command_name << std::endl;
    exit(-1);
  }
  while (optind < argc_)
    args.AddArgument(argv_[optind++]);
  try {
    command_item->second->Call(command_name, args);
  } catch (const Exception &error) {
    std::cerr << "ERROR: " << error.what() << std::endl;
    exit(-1);
  }
}

}  // namespace pud
