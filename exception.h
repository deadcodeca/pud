// Copyright (C) 2016, All rights reserved.
// Author: contem

#pragma once

#include <errno.h>
#include <string.h>
#include <exception>
#include <string>
#include <utility>

namespace pud {

class Exception : public std::exception {
 public:
  explicit Exception(std::string msg) : msg_(std::move(msg)) {}
  ~Exception() override {}

  const char *what() const noexcept override { return msg_.c_str(); }

 private:
  const std::string msg_;
};

#define CREATE_EXCEPTION(__name)                                    \
  class __name : public Exception {                                 \
   public:                                                          \
    explicit __name(std::string msg) : Exception(std::move(msg)) {} \
    ~__name() override {}                                           \
  };

CREATE_EXCEPTION(InternalError);
CREATE_EXCEPTION(InvalidArgument);
CREATE_EXCEPTION(ObjectAlreadyExists);
CREATE_EXCEPTION(OutOfRange);
CREATE_EXCEPTION(UnknownError);

class SystemError : public Exception {
 public:
  explicit SystemError(std::string msg)
      : Exception(std::move(msg) + ": " + strerror(errno)) {}
  ~SystemError() override {}
};

#undef CREATE_EXCEPTION

}  // namespace pud
