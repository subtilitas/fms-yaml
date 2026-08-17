// SPDX-License-Identifier: MIT
#include "fms/status.hpp"

#include <cstdio>
#include <cstdlib>

#include <etl/error_handler.h>

namespace fms {
namespace {

void etl_error_callback(const etl::exception& error) {
  std::fprintf(stderr, "[fms] ETL error: %s at %s:%d\n", error.what(), error.file_name(),
               static_cast<int>(error.line_number()));
  // ETL only reports programming errors here (a fixed container overrun, an
  // out-of-range access).  There is no sensible way to continue and we cannot
  // throw, so stop now and loudly.
  std::abort();
}

bool g_handler_installed = false;

}  // namespace

const char* to_string(Status status) noexcept {
  switch (status) {
    case Status::Ok:                 return "ok";
    case Status::FileNotFound:       return "config file not found";
    case Status::ParseError:         return "YAML parse error";
    case Status::SchemaError:        return "config does not match the schema";
    case Status::DuplicateName:      return "duplicate name";
    case Status::UnknownState:       return "unknown state";
    case Status::UnknownTrigger:     return "unknown trigger";
    case Status::NameTooLong:        return "name exceeds FMS_MAX_NAME_LENGTH";
    case Status::ChannelTooLong:     return "channel exceeds FMS_MAX_CHANNEL_LENGTH";
    case Status::CapacityExceeded:   return "a compile-time capacity was exceeded";
    case Status::NotInitialised:     return "not initialised";
    case Status::AlreadyInitialised: return "already initialised";
    case Status::InvalidArgument:    return "invalid argument";
    case Status::NoTransition:       return "trigger not accepted in this state";
    case Status::GuardRejected:      return "no guard held for this trigger";
    case Status::ArgumentError:      return "malformed trigger arguments";
    case Status::PortError:          return "port error";
    case Status::NotOpen:            return "port is not open";
    case Status::Timeout:            return "timeout";
    case Status::EndOfInput:         return "end of input";
  }
  return "unknown status";
}

void install_etl_error_handler() noexcept {
  if (g_handler_installed) {
    return;
  }
  etl::error_handler::set_callback<etl_error_callback>();
  g_handler_installed = true;
}

}  // namespace fms
