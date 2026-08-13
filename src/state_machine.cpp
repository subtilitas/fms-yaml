// SPDX-License-Identifier: MIT
#include "fms/state_machine.hpp"

namespace fms {

Status StateMachine::init(const Model& model) noexcept {
  install_etl_error_handler();

  if (started_) {
    return Status::AlreadyInitialised;
  }
  const Status validation = model.validate();
  if (!is_ok(validation)) {
    return validation;
  }

  model_            = &model;
  current_          = kNoState;
  transition_count_ = 0;
  rejection_count_  = 0;
  return Status::Ok;
}

Status StateMachine::start() noexcept {
  if (model_ == nullptr) {
    return Status::NotInitialised;
  }
  if (started_) {
    return Status::AlreadyInitialised;
  }
  if (!model_->has_state(model_->initial())) {
    return Status::UnknownState;
  }

  current_ = model_->initial();
  started_ = true;
  return Status::Ok;
}

Status StateMachine::fire(TriggerId trigger, TransitionEvent& out) noexcept {
  out = TransitionEvent{};

  if (model_ == nullptr || !started_) {
    return Status::NotInitialised;
  }

  out.from    = current_;
  out.trigger = trigger;

  // One binary search in the current state's transition table.  That is the
  // whole decision: a match is a state change, no match is an error.
  const StateId target = model_->target_of(current_, trigger);
  if (target == kNoState) {
    ++rejection_count_;
    return Status::NoTransition;
  }

  current_     = target;
  out.to       = target;
  out.accepted = true;
  ++transition_count_;
  return Status::Ok;
}

Status StateMachine::fire(TriggerId trigger) noexcept {
  TransitionEvent ignored;
  return fire(trigger, ignored);
}

const char* StateMachine::current_name() const noexcept {
  return (model_ == nullptr) ? "<uninitialised>" : model_->state_name(current_);
}

}  // namespace fms
