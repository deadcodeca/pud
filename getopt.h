// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "exception.h"
#include "util.h"

namespace pud {

class ArgumentMap {
 public:
  void SetFlag(const std::string &key, const std::string &value) {
    flags_[key] = value;
  }

  bool HasFlag(const std::string &key) const {
    return flags_.find(key) != flags_.end();
  }

  const std::string &GetFlag(const std::string &key) const {
    const auto it = flags_.find(key);
    if (it == flags_.end())
      throw InvalidArgument(Concat("Missing flag --", key));
    return it->second;
  }

  std::string GetFlagWithDefault(const std::string &key,
                                 const std::string &def = "") const {
    const auto it = flags_.find(key);
    if (it == flags_.end())
      return def;
    return it->second;
  }

  void AddArgument(const std::string &value) { args_.emplace_back(value); }

  size_t Count() const { return args_.size(); }

  const std::string &Arg(size_t index = 0) const {
    if (index >= args_.size())
      throw InvalidArgument("Not enough arguments for command");
    return args_[index];
  }

  const std::vector<std::string> Args() const { return args_; }

 private:
  std::vector<std::string> args_;
  std::map<std::string, std::string> flags_;
};

class Command {
 public:
  virtual ~Command() = default;
  virtual void Call(const char *command_name,
                    const ArgumentMap &args) const = 0;
  virtual std::unique_ptr<Command> Clone() const = 0;
};

class Execute : public Command {
 public:
  typedef std::function<void(const ArgumentMap &)> Callback;

  explicit Execute(Callback callback, size_t min_arg_count = 0)
      : callback_(std::move(callback)), min_arg_count_(min_arg_count) {}
  ~Execute() override {}

  void Call(const char *command_name, const ArgumentMap &args) const override;

  std::unique_ptr<Command> Clone() const override {
    return std::make_unique<Execute>(*this);
  }

 private:
  const Callback callback_;
  const size_t min_arg_count_;
};

class Option {
 public:
  virtual ~Option() = default;
  virtual void Call(const char *option_name, const char *option_value,
                    ArgumentMap *args) const = 0;
  virtual bool WantArgument() const = 0;
  virtual std::unique_ptr<Option> Clone() const = 0;
};

class Flag : public Option {
 public:
  explicit Flag(bool want_arg = false) : want_arg_(want_arg) {}

  void Call(const char *option_name, const char *option_value,
            ArgumentMap *args) const override;

  bool WantArgument() const override { return want_arg_; }

  std::unique_ptr<Option> Clone() const override {
    return std::make_unique<Flag>(*this);
  }

 private:
  const bool want_arg_;
};

class CommandLineParser {
 public:
  CommandLineParser(int argc, char **argv) : argc_(argc), argv_(argv) {}

  void SetDefaultCommand(const std::string &command_name);

  void AddCommand(const std::string &command_name, const Command &command);

  void AddCommand(const std::string &command_name, Execute::Callback callback,
                  size_t min_arg_count = 0);

  void AddOption(const std::string &option_name, const Option &option);

  void AddOption(const std::string &option_name, bool want_arg = false);

  void AddOptionAlias(const std::string &option_name, const std::string &alias);

  void Parse();

 private:
  const int argc_;
  char **argv_;

  std::string default_command_name_;
  std::map<std::string, std::unique_ptr<Command>> commands_;
  std::map<std::string, std::unique_ptr<Option>> options_;
  std::map<std::string, std::string> option_alias_;
};

}  // namespace pud
