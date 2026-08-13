// SPDX-License-Identifier: MIT
//
// Glue: an MQTT message on a trigger's topic becomes a trigger, the trigger
// becomes a state change, the state change is published.  A trigger the current
// state does not accept is published on the error topic instead.
#ifndef FMS_RUNTIME_HPP
#define FMS_RUNTIME_HPP

#include "fms/model.hpp"
#include "fms/mqtt/transport.hpp"
#include "fms/state_machine.hpp"

namespace fms {

/// Optional trace hook: called for every trigger, accepted or not.
using TraceFn = void (*)(void* user, const TransitionEvent&);

class Runtime {
 public:
  Runtime() noexcept = default;

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  /// Binds model, machine and transport.  All must outlive the Runtime.
  Status init(const Model& model, StateMachine& machine, mqtt::ITransport& transport) noexcept;

  /// Connects, subscribes to every trigger topic, enters the initial state and
  /// publishes it.  After this returns, the system does not allocate.
  Status start() noexcept;

  /// One iteration of the main loop: wait up to `poll_timeout_ms` for a message
  /// and dispatch it.  Returns Status::Ok even when nothing arrived.
  Status service(std::uint32_t poll_timeout_ms = 100) noexcept;

  Status stop() noexcept;

  void set_trace(TraceFn trace, void* user) noexcept;

  /// Raises a trigger by name from application code instead of from MQTT.
  Status fire_by_name(StringView trigger_name) noexcept;

  std::uint32_t messages_received() const noexcept { return messages_received_; }
  std::uint32_t messages_unrouted() const noexcept { return messages_unrouted_; }

  const StateMachine* machine() const noexcept { return machine_; }

 private:
  static void on_message_thunk(void* user, const mqtt::InboundMessage& message) noexcept;

  void   on_message(const mqtt::InboundMessage& message) noexcept;
  Status dispatch(TriggerId trigger) noexcept;
  void publish_state(StateId state) noexcept;
  void publish_rejection(StateId state, TriggerId trigger) noexcept;

  const Model*      model_     = nullptr;
  StateMachine*     machine_   = nullptr;
  mqtt::ITransport* transport_ = nullptr;

  TraceFn trace_      = nullptr;
  void*   trace_user_ = nullptr;

  /// Scratch buffer for the error payload; a member, so publishing a rejection
  /// touches no stack-heavy temporaries and no heap.
  Payload scratch_{};

  std::uint32_t messages_received_ = 0;
  std::uint32_t messages_unrouted_ = 0;
  bool          started_           = false;
};

}  // namespace fms

#endif  // FMS_RUNTIME_HPP
