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

  /// Binds an already initialised machine to a port; both must outlive the
  /// Runtime.  The model and the setup come from the machine, so there is no
  /// way to hand in a mismatched pair.
  Status init(StateMachine& machine, IPort& port) noexcept;

  /// Configures and opens the port, announces every trigger channel, enters the
  /// initial state and publishes it.  After this returns, nothing allocates.
  Status start() noexcept;

  /// One iteration of the main loop: wait up to `timeout_ms` for input and
  /// dispatch it.  Status::Ok when something was handled or nothing arrived,
  /// Status::EndOfInput when the port is exhausted.
  Status service(std::uint32_t timeout_ms = 100) noexcept;

  Status stop() noexcept;

  void set_trace(TraceFn trace, void* user) noexcept;

  /// Raises a trigger by name from application code instead of from the port.
  /// `arguments` is the same `key=value` text a port would deliver.
  Status fire_by_name(StringView trigger_name, StringView arguments = StringView{}) noexcept;

  std::uint32_t inputs_received() const noexcept { return inputs_received_; }
  std::uint32_t inputs_unrouted() const noexcept { return inputs_unrouted_; }
  std::uint32_t inputs_rejected() const noexcept { return inputs_rejected_; }

  /// The arguments of the most recent dispatch.  Only valid until the next
  /// service() call, since the values are views into the port's buffer.
  const Args& last_arguments() const noexcept { return args_; }

  const StateMachine* machine() const noexcept { return machine_; }

 private:
  Status dispatch(TriggerId trigger, const Args& args) noexcept;
  void   publish_state(StateId state) noexcept;
  void   publish_rejection(const TransitionEvent& event, const Args& args) noexcept;
  void   publish_unknown(StringView channel) noexcept;
  void   publish_bad_arguments(TriggerId trigger, Status reason) noexcept;

  const Model*  model_   = nullptr;
  const Setup*  setup_   = nullptr;
  StateMachine* machine_ = nullptr;
  IPort*        port_    = nullptr;

  TraceFn trace_      = nullptr;
  void*   trace_user_ = nullptr;

  /// Scratch buffer for error text; a member, so reporting a rejection touches
  /// no temporaries and no heap.
  Message scratch_{};

  /// Arguments of the dispatch in flight.  A member for the same reason: the
  /// map is fixed capacity and the values are views, so parsing costs nothing.
  Args args_{};

  std::uint32_t inputs_received_ = 0;
  std::uint32_t inputs_unrouted_ = 0;
  std::uint32_t inputs_rejected_ = 0;
  bool          started_         = false;
};

}  // namespace fms

#endif  // FMS_RUNTIME_HPP
