// SPDX-License-Identifier: MIT
#include "fms/status.hpp"

#include <cstdlib>

#include <etl/error_handler.h>

// A program with an operating system has a stderr to report on, and std::cerr
// writes there without allocating; tests/test_no_alloc.cpp counts the
// allocations of such a write.  A bare-metal image has no stderr until the
// application provides one, and std::cerr would link all of <iostream> into
// it.  A Cortex-M4 probe image that installs the ETL handler has 295 KB of
// text with std::cerr, 39 KB with fprintf and 16 KB with neither (newlib), and
// with std::cerr it does not link against newlib-nano at all.  There the
// report is left out and the abort stays.
#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
#define FMS_REPORT_ON_STDERR 1
#include <iostream>
#else
#define FMS_REPORT_ON_STDERR 0
#endif

namespace fms {
namespace {

void etl_error_callback(const etl::exception& error) {
#if FMS_REPORT_ON_STDERR
  std::cerr << "[fms] ETL error: " << error.what() << " at " << error.file_name() << ':'
            << error.line_number() << '\n';
#else
  (void)error;
#endif
  // ETL only reports programming errors here (a fixed container overrun, an
  // out-of-range access).  There is no sensible way to continue and nothing
  // may throw, so stop now and loudly.
  std::abort();
}

bool g_handler_installed = false;

}  // namespace

const char* to_string(Status status) noexcept {
  switch (status) {
    case Status::Ok:                 return "ok";
    case Status::FileNotFound:       return "config file not found";
    case Status::FileNotReadable:    return "config file cannot be read";
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
