// SPDX-License-Identifier: MIT
//
// The machine: a model, a starting point from the setup, and a current state.
//
//   fire(trigger, args) ->  Status::Ok            an alternative held; the state
//                                                 changed, see the event
//                       ->  Status::NoTransition  the current state does not list
//                                                 this trigger at all
//                       ->  Status::GuardRejected it lists it, but no guard held
//
// In every rejecting case the state is unchanged.  Guards are declarative
// comparisons over the trigger's arguments (see condition.hpp), so there is
// still no application code in the decision - and no actions, no timers, no
// hierarchy.  Nothing here allocates, throws or blocks.
#ifndef FMS_STATE_MACHINE_HPP
#define FMS_STATE_MACHINE_HPP

#include "fms/model.hpp"
#include "fms/setup.hpp"

namespace fms {

/// The outcome of a fire() call.  On rejection `to` stays kNoState and `from`
/// is the state that refused the trigger.
struct TransitionEvent {
  StateId   from     = kNoState;
  StateId   to       = kNoState;
  TriggerId trigger  = kNoTrigger;
  bool      accepted = false;
  /// True when the state does list the trigger but no guard held - worth
  /// telling apart from "unknown here", because it usually means the arguments
  /// were not what the machine was waiting for.
  bool guard_rejected = false;
};

class StateMachine {
 public:
  StateMachine() noexcept = default;

  StateMachine(const StateMachine&) = delete;
  StateMachine& operator=(const StateMachine&) = delete;

  /// Binds the two halves of the configuration together.  Both must outlive the
  /// machine.  This is where the one cross-file reference is checked: the
  /// initial state named by the setup must exist in the model.
  Status init(const Model& model, const Setup& setup) noexcept;

  /// Enters the initial state.
  Status start() noexcept;

  /// Applies a trigger together with the arguments it carried.  The arguments
  /// are only read during the call - guards look at them, the machine does not
  /// keep them.
  Status fire(TriggerId trigger, const Args& args, TransitionEvent& out) noexcept;

  /// Convenience overloads: no arguments, and/or no interest in the detail.
  Status fire(TriggerId trigger, TransitionEvent& out) noexcept;
  Status fire(TriggerId trigger, const Args& args) noexcept;
  Status fire(TriggerId trigger) noexcept;

  bool         started() const noexcept { return started_; }
  StateId      current() const noexcept { return current_; }
  const char*  current_name() const noexcept;
  const Model* model() const noexcept { return model_; }
  const Setup* setup() const noexcept { return setup_; }
  StateId      initial() const noexcept { return initial_; }

  std::uint32_t transition_count() const noexcept { return transition_count_; }
  std::uint32_t rejection_count() const noexcept { return rejection_count_; }

 private:
  const Model* model_ = nullptr;
  const Setup* setup_ = nullptr;

  StateId initial_ = kNoState;  ///< resolved once, in init()
  StateId current_ = kNoState;
  bool    started_ = false;

  std::uint32_t transition_count_ = 0;
  std::uint32_t rejection_count_  = 0;
};

}  // namespace fms

#endif  // FMS_STATE_MACHINE_HPP
