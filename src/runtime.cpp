// SPDX-License-Identifier: MIT
#include "fms/runtime.hpp"

#include <cstring>

namespace fms {
namespace {

StringView cstr(const char* text) noexcept { return StringView(text, std::strlen(text)); }

}  // namespace

Status Runtime::init(const Model& model, StateMachine& machine,
                     mqtt::ITransport& transport) noexcept {
  if (started_) {
    return Status::AlreadyInitialised;
  }
  model_     = &model;
  machine_   = &machine;
  transport_ = &transport;
  transport_->set_handler(&Runtime::on_message_thunk, this);
  return Status::Ok;
}

void Runtime::set_trace(TraceFn trace, void* user) noexcept {
  trace_      = trace;
  trace_user_ = user;
}

Status Runtime::start() noexcept {
  if (model_ == nullptr || machine_ == nullptr || transport_ == nullptr) {
    return Status::NotInitialised;
  }
  if (started_) {
    return Status::AlreadyInitialised;
  }

  const Status connected = transport_->connect();
  if (!is_ok(connected)) {
    return connected;
  }

  for (const auto& entry : model_->topic_index()) {
    const Status subscribed = transport_->subscribe(view(entry.first), model_->mqtt().qos);
    if (!is_ok(subscribed)) {
      return subscribed;
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

Status Runtime::service(std::uint32_t poll_timeout_ms) noexcept {
  if (!started_) {
    return Status::NotInitialised;
  }
  const Status polled = transport_->poll(poll_timeout_ms);
  if (!is_ok(polled) && polled != Status::Timeout) {
    return polled;
  }
  return Status::Ok;
}

Status Runtime::stop() noexcept {
  if (transport_ == nullptr) {
    return Status::NotInitialised;
  }
  started_ = false;
  return transport_->disconnect();
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

void Runtime::on_message_thunk(void* user, const mqtt::InboundMessage& message) noexcept {
  static_cast<Runtime*>(user)->on_message(message);
}

void Runtime::on_message(const mqtt::InboundMessage& message) noexcept {
  ++messages_received_;

  // One topic per trigger: routing is a single lookup, and the payload is not
  // part of the decision.
  const TriggerId trigger = model_->find_trigger_for_topic(message.topic);
  if (trigger == kNoTrigger) {
    ++messages_unrouted_;
    return;
  }
  dispatch(trigger);
}

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
  const MqttConfig& mqtt = model_->mqtt();
  if (mqtt.state_topic.empty()) {
    return;
  }
  const StateNode* node = model_->state(state);
  if (node == nullptr) {
    return;
  }
  transport_->publish(view(mqtt.state_topic), view(node->name), mqtt.qos, mqtt.retain_state);
}

void Runtime::publish_rejection(StateId state, TriggerId trigger) noexcept {
  const MqttConfig& mqtt = model_->mqtt();
  if (mqtt.error_topic.empty()) {
    return;
  }

  // "rejected: <trigger> in state <state>" - built in a fixed member buffer.
  scratch_.clear();
  append_clipped(scratch_, cstr("rejected: "));
  append_clipped(scratch_, cstr(model_->trigger_name(trigger)));
  append_clipped(scratch_, cstr(" in state "));
  append_clipped(scratch_, cstr(model_->state_name(state)));

  transport_->publish(view(mqtt.error_topic), view(scratch_), mqtt.qos, /*retain=*/false);
}

}  // namespace fms
