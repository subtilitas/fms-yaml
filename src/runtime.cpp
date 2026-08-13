// SPDX-License-Identifier: MIT
#include "fms/runtime.hpp"

#include <cstring>

namespace fms {
namespace {

StringView cstr(const char* text) noexcept { return StringView(text, std::strlen(text)); }

}  // namespace

Status Runtime::init(StateMachine& machine, IPort& port) noexcept {
  if (started_) {
    return Status::AlreadyInitialised;
  }
  if (machine.model() == nullptr || machine.setup() == nullptr) {
    return Status::NotInitialised;  // call StateMachine::init first
  }
  model_   = machine.model();
  setup_   = machine.setup();
  machine_ = &machine;
  port_    = &port;
  return Status::Ok;
}

void Runtime::set_trace(TraceFn trace, void* user) noexcept {
  trace_      = trace;
  trace_user_ = user;
}

Status Runtime::start() noexcept {
  if (model_ == nullptr || setup_ == nullptr || machine_ == nullptr || port_ == nullptr) {
    return Status::NotInitialised;
  }
  if (started_) {
    return Status::AlreadyInitialised;
  }

  // The setup half of the config reaches the port here, and only here.
  const Status configured = port_->configure(setup_->io());
  if (!is_ok(configured)) {
    return configured;
  }

  const Status opened = port_->open();
  if (!is_ok(opened)) {
    return opened;
  }

  for (const auto& entry : model_->channel_index()) {
    const Status listening = port_->listen(view(entry.first));
    if (!is_ok(listening)) {
      return listening;
    }
  }

  const Status started = machine_->start();
  if (!is_ok(started)) {
    return started;
  }

  started_ = true;
  publish_state(machine_->current());
  return Status::Ok;
}

Status Runtime::service(std::uint32_t timeout_ms) noexcept {
  if (!started_) {
    return Status::NotInitialised;
  }

  StringView   channel;
  const Status received = port_->receive(channel, timeout_ms);
  if (received == Status::Timeout) {
    return Status::Ok;  // idle, not an error
  }
  if (!is_ok(received)) {
    return received;    // EndOfInput or a real failure
  }

  ++inputs_received_;

  // One channel per trigger: routing is a single lookup.
  const TriggerId trigger = model_->find_trigger_for_channel(channel);
  if (trigger == kNoTrigger) {
    ++inputs_unrouted_;
    publish_unknown(channel);
    return Status::Ok;
  }

  dispatch(trigger);
  return Status::Ok;
}

Status Runtime::stop() noexcept {
  if (port_ == nullptr) {
    return Status::NotInitialised;
  }
  started_ = false;
  return port_->close();
}

Status Runtime::fire_by_name(StringView trigger_name) noexcept {
  if (!started_) {
    return Status::NotInitialised;
  }
  const TriggerId trigger = model_->find_trigger(trigger_name);
  if (trigger == kNoTrigger) {
    return Status::UnknownTrigger;
  }
  return dispatch(trigger);
}

// ---------------------------------------------------------------------------
// internals
// ---------------------------------------------------------------------------

Status Runtime::dispatch(TriggerId trigger) noexcept {
  TransitionEvent event;
  const Status    status = machine_->fire(trigger, event);

  if (is_ok(status)) {
    publish_state(event.to);
  } else if (status == Status::NoTransition) {
    publish_rejection(event.from, trigger);
  }

  if (trace_ != nullptr) {
    trace_(trace_user_, event);
  }
  return status;
}

void Runtime::publish_state(StateId state) noexcept {
  const StateNode* node = model_->state(state);
  if (node != nullptr) {
    port_->publish_state(view(node->name));
  }
}

void Runtime::publish_rejection(StateId state, TriggerId trigger) noexcept {
  // "rejected: <trigger> in state <state>" - built in a fixed member buffer.
  scratch_.clear();
  append_clipped(scratch_, cstr("rejected: "));
  append_clipped(scratch_, cstr(model_->trigger_name(trigger)));
  append_clipped(scratch_, cstr(" in state "));
  append_clipped(scratch_, cstr(model_->state_name(state)));
  port_->publish_error(view(scratch_));
}

void Runtime::publish_unknown(StringView channel) noexcept {
  scratch_.clear();
  append_clipped(scratch_, cstr("unknown channel: "));
  append_clipped(scratch_, channel);
  port_->publish_error(view(scratch_));
}

}  // namespace fms
