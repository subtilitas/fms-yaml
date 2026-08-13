// SPDX-License-Identifier: MIT
//
// The seam between the state machine and the outside world.
//
// The core knows nothing about how triggers arrive.  It deals in *channels*:
// opaque strings that identify where something came from.  A channel is an MQTT
// topic, a CAN identifier, a UDP port, a line typed on stdin - whatever the
// port decides.  Each trigger declares the channel that raises it (defaulting
// to its own name), and the port is free to interpret that string.
//
// To wire the machine to something new, implement this interface.  That is the
// only file you need to look at:
//
//   class MyPort final : public fms::IPort {
//     fms::Status open() noexcept override            { ... }
//     fms::Status listen(fms::StringView ch) noexcept override  { ... }  // optional
//     fms::Status receive(fms::StringView& ch, uint32_t ms) noexcept override { ... }
//     fms::Status publish_state(fms::StringView s) noexcept override { ... }
//     fms::Status publish_error(fms::StringView m) noexcept override { ... }
//     fms::Status close() noexcept override           { ... }
//   };
//
// Contract:
//   * No method may throw or allocate.
//   * receive() is the only call that may block, and only up to timeout_ms.
//   * The view returned by receive() must stay valid until the next call on the
//     port, which means the port owns the buffer - the core only reads it.
#ifndef FMS_PORT_HPP
#define FMS_PORT_HPP

#include <cstdint>

#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms {

class IPort {
 public:
  virtual ~IPort() = default;

  /// Opens the connection, device or file.  Called once by Runtime::start().
  virtual Status open() noexcept { return Status::Ok; }

  /// Closes it again.  Called by Runtime::stop().
  virtual Status close() noexcept { return Status::Ok; }

  /// Announces one channel the machine cares about.  Called once per trigger
  /// during start(), before any receive().  A broker-backed port subscribes
  /// here; a port that reads everything anyway can ignore it.
  virtual Status listen(StringView /*channel*/) noexcept { return Status::Ok; }

  /// Waits up to `timeout_ms` for input.
  ///   Status::Ok          - `channel` names where the input came from
  ///   Status::Timeout     - nothing arrived; a normal, non-error outcome
  ///   Status::EndOfInput  - the source is exhausted (EOF, peer closed)
  ///   anything else       - a real failure
  virtual Status receive(StringView& channel, std::uint32_t timeout_ms) noexcept = 0;

  /// Reports the state the machine has just entered.
  virtual Status publish_state(StringView state) noexcept = 0;

  /// Reports a trigger the current state refused, or an unknown channel.
  virtual Status publish_error(StringView message) noexcept = 0;

  /// Human readable detail about the last failure; never null.
  virtual const char* last_error() const noexcept { return "none"; }

 protected:
  IPort() = default;
  IPort(const IPort&) = default;
  IPort& operator=(const IPort&) = default;
};

}  // namespace fms

#endif  // FMS_PORT_HPP
