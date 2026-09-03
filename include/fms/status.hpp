// SPDX-License-Identifier: MIT
//
// No exceptions are used anywhere, so every fallible operation returns a Status.
#ifndef FMS_STATUS_HPP
#define FMS_STATUS_HPP

#include <cstdint>

namespace fms {

enum class Status : std::uint8_t {
  Ok = 0,

  // --- configuration -------------------------------------------------------
  FileNotFound,
  FileNotReadable,  ///< it opens and the first read fails: a directory, a
                    ///< device that refuses to be read, a revoked mount
  ParseError,       ///< the YAML text itself is malformed
  SchemaError,      ///< valid YAML, but not a valid machine description
  DuplicateName,
  UnknownState,
  UnknownTrigger,
  NameTooLong,
  ChannelTooLong,
  CapacityExceeded, ///< one of the fms::limits ceilings was hit

  // --- runtime -------------------------------------------------------------
  NotInitialised,
  AlreadyInitialised,
  InvalidArgument,
  NoTransition,     ///< the current state does not accept this trigger
  GuardRejected,    ///< it does accept it, but no guard held
  ArgumentError,    ///< malformed trigger arguments

  // --- port ----------------------------------------------------------------
  PortError,
  NotOpen,
  Timeout,          ///< nothing arrived within the poll window (not an error)
  EndOfInput,       ///< the input source is exhausted
};

/// Static, allocation-free description of a status code.
const char* to_string(Status status) noexcept;

constexpr bool is_ok(Status status) noexcept { return status == Status::Ok; }

/// Installs a stderr-logging, abort()-ing handler for ETL's internal error
/// reports (ETL is built without exceptions and reports through a callback).
/// Idempotent; called automatically by StateMachine::init and the YAML loader.
void install_etl_error_handler() noexcept;

}  // namespace fms

#endif  // FMS_STATUS_HPP
