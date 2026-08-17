// SPDX-License-Identifier: MIT
#include "fms/state_machine.hpp"

namespace fms {

Status StateMachine::init(const Model& model, const Setup& setup) noexcept {
  install_etl_error_handler();

  if (started_) {
    return Status::AlreadyInitialised;
  }
  const Status validation = model.validate();
  if (!is_ok(validation)) {
    return validation;
  }
  // The two files were loaded independently; this is the moment they have to
  // agree.  A setup naming a state the machine does not have fails here, at
  // start-up, not on the first trigger.
  const Status binding = setup.validate_against(model);
  if (!is_ok(binding)) {
    return binding;
  }

  model_            = &model;
  setup_            = &setup;
  initial_          = setup.initial_in(model);
  current_          = kNoState;
  transition_count_ = 0;
  rejection_count_  = 0;
  return Status::Ok;
}

Status StateMachine::start() noexcept {
  if (model_ == nullptr || setup_ == nullptr) {
    return Status::NotInitialised;
  }
  if (started_) {
    return Status::AlreadyInitialised;
  }
  if (!model_->has_state(initial_)) {
    return Status::UnknownState;
  }

  current_ = initial_;
  started_ = true;
  return Status::Ok;
}

Status StateMachine::fire(TriggerId trigger, const Args& args, TransitionEvent& out) noexcept {
  out = TransitionEvent{};

  if (model_ == nullptr || !started_) {
    return Status::NotInitialised;
  }

  out.from    = current_;
  out.trigger = trigger;

  // One binary search in the current state's transition table, then the
  // alternatives in file order.  That is the whole decision.
  StateId        target   = kNoState;
  const Decision decision = model_->evaluate(current_, trigger, args, target);

  if (decision == Decision::Accepted) {
    current_     = target;
    out.to       = target;
    out.accepted = true;
    ++transition_count_;
    return Status::Ok;
  }

  ++rejection_count_;
  if (decision == Decision::GuardRejected) {
    out.guard_rejected = true;
    return Status::GuardRejected;
  }
  return Status::NoTransition;
}

Status StateMachine::fire(TriggerId trigger, TransitionEvent& out) noexcept {
  return fire(trigger, Args::none(), out);
}

Status StateMachine::fire(TriggerId trigger, const Args& args) noexcept {
  TransitionEvent ignored;
  return fire(trigger, args, ignored);
}

Status StateMachine::fire(TriggerId trigger) noexcept {
  TransitionEvent ignored;
  return fire(trigger, Args::none(), ignored);
}

const char* StateMachine::current_name() const noexcept {
  return (model_ == nullptr) ? "<uninitialised>" : model_->state_name(current_);
}

}  // namespace fms
