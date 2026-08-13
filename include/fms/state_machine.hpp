// SPDX-License-Identifier: MIT
//
// The machine: a pointer to a Model and a current state.
//
//   fire(trigger)  ->  Status::Ok           the state changed, see the event
//                  ->  Status::NoTransition the current state does not accept
//                                           this trigger; the state is unchanged
//
// That is the entire behaviour.  No guards, no actions, no timers, no
// hierarchy.  Nothing here allocates, throws or blocks.
#ifndef FMS_STATE_MACHINE_HPP
#define FMS_STATE_MACHINE_HPP

#include "fms/model.hpp"

namespace fms {

/// The outcome of a fire() call.  On rejection `to` stays kNoState and `from`
/// is the state that refused the trigger.
struct TransitionEvent {
  StateId   from    = kNoState;
  StateId   to      = kNoState;
  TriggerId trigger = kNoTrigger;
  bool      accepted = false;
};

class StateMachine {
 public:
  StateMachine() noexcept = default;

  StateMachine(const StateMachine&) = delete;
  StateMachine& operator=(const StateMachine&) = delete;

  /// Binds the machine to a model, which must outlive it.
  Status init(const Model& model) noexcept;

  /// Enters the initial state.
  Status start() noexcept;

  /// Applies a trigger.
  Status fire(TriggerId trigger, TransitionEvent& out) noexcept;

  /// Convenience overload for callers that do not need the detail.
  Status fire(TriggerId trigger) noexcept;

  bool         started() const noexcept { return started_; }
  StateId      current() const noexcept { return current_; }
  const char*  current_name() const noexcept;
  const Model* model() const noexcept { return model_; }

  std::uint32_t transition_count() const noexcept { return transition_count_; }
  std::uint32_t rejection_count() const noexcept { return rejection_count_; }

 private:
  const Model* model_   = nullptr;
  StateId      current_ = kNoState;
  bool         started_ = false;

  std::uint32_t transition_count_ = 0;
  std::uint32_t rejection_count_  = 0;
};

}  // namespace fms

#endif  // FMS_STATE_MACHINE_HPP
