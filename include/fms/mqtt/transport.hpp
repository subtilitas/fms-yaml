// SPDX-License-Identifier: MIT
//
// The seam between the state machine and MQTT.
//
// Nothing above this interface knows that paho exists: the FSM, the loader and
// the runtime only ever see ITransport.  That buys two things - the machine can
// be unit tested against LoopbackTransport with no broker running, and paho can
// be swapped for another client (or a bare TCP/serial link) by writing one more
// implementation.
//
// Contract:
//   * All methods are non-blocking except connect() and poll(timeout_ms).
//   * Payload buffers handed to the handler are only valid for the duration of
//     the callback.  Copy what you need.
//   * No method allocates, throws, or spawns threads.  poll() is the only place
//     inbound work happens, so the whole system is single threaded by default.
#ifndef FMS_MQTT_TRANSPORT_HPP
#define FMS_MQTT_TRANSPORT_HPP

#include <cstdint>

#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms::mqtt {

struct InboundMessage {
  StringView   topic{};
  StringView   payload{};
  std::uint8_t qos      = 0;
  bool         retained = false;
};

/// Invoked from within poll() for each message that arrived.
using MessageHandler = void (*)(void* user, const InboundMessage&);

class ITransport {
 public:
  virtual ~ITransport() = default;

  /// Opens the connection and installs the last will, if configured.
  virtual Status connect() noexcept = 0;
  virtual Status disconnect() noexcept = 0;
  virtual bool   connected() const noexcept = 0;

  /// Subscribes to one trigger topic.
  virtual Status subscribe(StringView topic, std::uint8_t qos) noexcept = 0;

  virtual Status publish(StringView topic, StringView payload, std::uint8_t qos,
                         bool retain) noexcept = 0;

  /// Waits up to `timeout_ms` for inbound traffic and dispatches whatever
  /// arrived to the handler.  Returns Status::Timeout when nothing came in,
  /// which is a normal, non-error outcome.
  virtual Status poll(std::uint32_t timeout_ms) noexcept = 0;

  virtual void set_handler(MessageHandler handler, void* user) noexcept = 0;

  /// Human readable detail about the last failure; never null.
  virtual const char* last_error() const noexcept = 0;

 protected:
  ITransport() = default;
  ITransport(const ITransport&) = default;
  ITransport& operator=(const ITransport&) = default;
};

}  // namespace fms::mqtt

#endif  // FMS_MQTT_TRANSPORT_HPP
