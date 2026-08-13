// SPDX-License-Identifier: MIT
#include "fms/model.hpp"

namespace fms {
namespace {
constexpr const char* kInvalid = "<invalid>";
}

void Model::clear() noexcept {
  name_.clear();
  initial_ = kNoState;
  mqtt_ = MqttConfig{};
  states_.clear();
  state_index_.clear();
  triggers_.clear();
  trigger_index_.clear();
  topic_index_.clear();
}

Status Model::set_name(StringView name) noexcept {
  return assign_checked(name_, name) ? Status::Ok : Status::NameTooLong;
}

Status Model::declare_state(StringView name, StateId& out_id) noexcept {
  out_id = kNoState;

  if (name.empty()) {
    return Status::InvalidArgument;
  }
  if (states_.full() || state_index_.full()) {
    return Status::CapacityExceeded;
  }

  Name key;
  if (!assign_checked(key, name)) {
    return Status::NameTooLong;
  }
  if (state_index_.find(key) != state_index_.end()) {
    return Status::DuplicateName;
  }

  const StateId id = static_cast<StateId>(states_.size());

  StateNode node;
  node.name = key;
  states_.insert(StateMap::value_type(id, node));
  state_index_.insert(StateIndex::value_type(key, id));

  out_id = id;
  return Status::Ok;
}

Status Model::declare_trigger(StringView name, StringView topic, TriggerId& out_id) noexcept {
  out_id = kNoTrigger;

  if (name.empty()) {
    return Status::InvalidArgument;
  }
  if (triggers_.full() || trigger_index_.full() || topic_index_.full()) {
    return Status::CapacityExceeded;
  }

  TriggerDef def;
  if (!assign_checked(def.name, name)) {
    return Status::NameTooLong;
  }
  if (!assign_checked(def.topic, topic)) {
    return Status::TopicTooLong;
  }
  if (trigger_index_.find(def.name) != trigger_index_.end()) {
    return Status::DuplicateName;
  }
  // One topic per trigger, and one trigger per topic: two subsystems sharing a
  // topic would make routing ambiguous, so it is rejected here.
  if (!def.topic.empty() && topic_index_.find(def.topic) != topic_index_.end()) {
    return Status::DuplicateName;
  }

  const TriggerId id = static_cast<TriggerId>(triggers_.size());
  triggers_.insert(TriggerMap::value_type(id, def));
  trigger_index_.insert(TriggerIndex::value_type(def.name, id));
  if (!def.topic.empty()) {
    topic_index_.insert(TopicIndex::value_type(def.topic, id));
  }

  out_id = id;
  return Status::Ok;
}

Status Model::add_transition(StateId from, TriggerId trigger, StateId target) noexcept {
  const auto it = states_.find(from);
  if (it == states_.end()) {
    return Status::UnknownState;
  }
  if (triggers_.find(trigger) == triggers_.end()) {
    return Status::UnknownTrigger;
  }
  if (!has_state(target)) {
    return Status::UnknownState;
  }

  StateNode::TransitionMap& transitions = it->second.transitions;
  if (transitions.find(trigger) != transitions.end()) {
    return Status::DuplicateName;  // the same trigger twice in one state
  }
  if (transitions.full()) {
    return Status::CapacityExceeded;
  }
  transitions.insert(StateNode::TransitionMap::value_type(trigger, target));
  return Status::Ok;
}

Status Model::set_initial(StateId id) noexcept {
  if (!has_state(id)) {
    return Status::UnknownState;
  }
  initial_ = id;
  return Status::Ok;
}

Status Model::validate() const noexcept {
  if (states_.empty()) {
    return Status::SchemaError;
  }
  if (!has_state(initial_)) {
    return Status::UnknownState;
  }

  for (const auto& entry : states_) {
    for (const auto& transition : entry.second.transitions) {
      if (triggers_.find(transition.first) == triggers_.end()) {
        return Status::UnknownTrigger;
      }
      if (!has_state(transition.second)) {
        return Status::UnknownState;
      }
    }
  }
  return Status::Ok;
}

bool Model::has_state(StateId id) const noexcept {
  return states_.find(id) != states_.end();
}

const StateNode* Model::state(StateId id) const noexcept {
  const auto it = states_.find(id);
  return (it == states_.end()) ? nullptr : &it->second;
}

const TriggerDef* Model::trigger(TriggerId id) const noexcept {
  const auto it = triggers_.find(id);
  return (it == triggers_.end()) ? nullptr : &it->second;
}

StateId Model::find_state(StringView name) const noexcept {
  Name key;
  if (!assign_checked(key, name)) {
    return kNoState;
  }
  const auto it = state_index_.find(key);
  return (it == state_index_.end()) ? kNoState : it->second;
}

TriggerId Model::find_trigger(StringView name) const noexcept {
  Name key;
  if (!assign_checked(key, name)) {
    return kNoTrigger;
  }
  const auto it = trigger_index_.find(key);
  return (it == trigger_index_.end()) ? kNoTrigger : it->second;
}

TriggerId Model::find_trigger_for_topic(StringView topic) const noexcept {
  Topic key;
  if (!assign_checked(key, topic)) {
    return kNoTrigger;
  }
  const auto it = topic_index_.find(key);
  return (it == topic_index_.end()) ? kNoTrigger : it->second;
}

const char* Model::state_name(StateId id) const noexcept {
  const StateNode* node = state(id);
  return (node == nullptr) ? kInvalid : node->name.c_str();
}

const char* Model::trigger_name(TriggerId id) const noexcept {
  const TriggerDef* def = trigger(id);
  return (def == nullptr) ? kInvalid : def->name.c_str();
}

StateId Model::target_of(StateId from, TriggerId trigger) const noexcept {
  const StateNode* node = state(from);
  if (node == nullptr) {
    return kNoState;
  }
  const auto it = node->transitions.find(trigger);
  return (it == node->transitions.end()) ? kNoState : it->second;
}

}  // namespace fms
