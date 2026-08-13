// SPDX-License-Identifier: MIT
//
// Glue: input on a trigger's channel becomes a trigger, the trigger becomes a
// state change, the state change is published.  A trigger the current state
// does not accept, or a channel nothing listens on, is published as an error.
//
// The Runtime pulls: service() asks the port for input and blocks only there.
// No threads, no callbacks, no queues.
#ifndef FMS_RUNTIME_HPP
#define FMS_RUNTIME_HPP

#include "fms/model.hpp"
#include "fms/port.hpp"
#include "fms/state_machine.hpp"

namespace fms {

/// Optional trace hook: called for every trigger, accepted or not.
using TraceFn = void (*)(void* user, const TransitionEvent&);

class Runtime {
 public:
  Runtime() noexcept = default;

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  /// Binds model, machine and port.  All must outlive the Runtime.
  Status init(const Model& model, StateMachine& machine, IPort& port) noexcept;

  /// Opens the port, announces every trigger channel, enters the initial state
  /// and publishes it.  After this returns, the system does not allocate.
  Status start() noexcept;

  /// One iteration of the main loop: wait up to `timeout_ms` for input and
  /// dispatch it.  Status::Ok when something was handled or nothing arrived,
  /// Status::EndOfInput when the port is exhausted.
  Status service(std::uint32_t timeout_ms = 100) noexcept;

  Status stop() noexcept;

  void set_trace(TraceFn trace, void* user) noexcept;

  /// Raises a trigger by name from application code instead of from the port.
  Status fire_by_name(StringView trigger_name) noexcept;

  std::uint32_t inputs_received() const noexcept { return inputs_received_; }
  std::uint32_t inputs_unrouted() const noexcept { return inputs_unrouted_; }

  const StateMachine* machine() const noexcept { return machine_; }

 private:
  Status dispatch(TriggerId trigger) noexcept;
  void   publish_state(StateId state) noexcept;
  void   publish_rejection(StateId state, TriggerId trigger) noexcept;
  void   publish_unknown(StringView channel) noexcept;

  const Model*  model_   = nullptr;
  StateMachine* machine_ = nullptr;
  IPort*        port_    = nullptr;

  TraceFn trace_      = nullptr;
  void*   trace_user_ = nullptr;

  /// Scratch buffer for error text; a member, so reporting a rejection touches
  /// no temporaries and no heap.
  Message scratch_{};

  std::uint32_t inputs_received_ = 0;
  std::uint32_t inputs_unrouted_ = 0;
  bool          started_         = false;
};

}  // namespace fms

#endif  // FMS_RUNTIME_HPP
