// SPDX-License-Identifier: MIT
#include "fms/model.hpp"

namespace fms {
namespace {
constexpr const char* kInvalid = "<invalid>";
}

void Model::clear() noexcept {
  name_.clear();
  states_.clear();
  state_index_.clear();
  triggers_.clear();
  trigger_index_.clear();
  channel_index_.clear();
  conditions_.clear();
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

Status Model::declare_trigger(StringView name, StringView channel, TriggerId& out_id) noexcept {
  out_id = kNoTrigger;

  if (name.empty()) {
    return Status::InvalidArgument;
  }
  if (triggers_.full() || trigger_index_.full() || channel_index_.full()) {
    return Status::CapacityExceeded;
  }

  TriggerDef def;
  if (!assign_checked(def.name, name)) {
    return Status::NameTooLong;
  }
  // No channel given: the trigger listens on its own name, which is what a
  // console or a plain text protocol wants.
  if (!assign_checked(def.channel, channel.empty() ? name : channel)) {
    return Status::ChannelTooLong;
  }
  if (trigger_index_.find(def.name) != trigger_index_.end()) {
    return Status::DuplicateName;
  }
  // One channel per trigger, one trigger per channel: two sources sharing a
  // channel would make routing ambiguous, so it is rejected here.
  if (channel_index_.find(def.channel) != channel_index_.end()) {
    return Status::DuplicateName;
  }

  const TriggerId id = static_cast<TriggerId>(triggers_.size());
  triggers_.insert(TriggerMap::value_type(id, def));
  trigger_index_.insert(TriggerIndex::value_type(def.name, id));
  channel_index_.insert(ChannelIndex::value_type(def.channel, id));

  out_id = id;
  return Status::Ok;
}

Alternatives* Model::alternatives_for(StateId from, TriggerId trigger) noexcept {
  const auto state_it = states_.find(from);
  if (state_it == states_.end()) {
    return nullptr;
  }
  StateNode::TransitionMap& transitions = state_it->second.transitions;

  auto it = transitions.find(trigger);
  if (it == transitions.end()) {
    if (transitions.full()) {
      return nullptr;
    }
    it = transitions.insert(StateNode::TransitionMap::value_type(trigger, Alternatives{})).first;
  }
  return &it->second;
}

Status Model::add_transition(StateId from, TriggerId trigger, StateId target) noexcept {
  return add_transition(from, trigger, target, ConditionList{});
}

Status Model::add_transition(StateId from, TriggerId trigger, StateId target,
                             const ConditionList& conditions) noexcept {
  if (!has_state(from)) {
    return Status::UnknownState;
  }
  if (triggers_.find(trigger) == triggers_.end()) {
    return Status::UnknownTrigger;
  }
  if (!has_state(target)) {
    return Status::UnknownState;
  }
  if (conditions_.size() + conditions.size() > conditions_.max_size()) {
    return Status::CapacityExceeded;
  }

  Alternatives* alternatives = alternatives_for(from, trigger);
  if (alternatives == nullptr) {
    return Status::CapacityExceeded;  // no room for another trigger in this state
  }
  if (alternatives->full()) {
    return Status::CapacityExceeded;  // no room for another alternative
  }

  Alternative alternative;
  alternative.target          = target;
  alternative.first_condition = static_cast<std::uint16_t>(conditions_.size());
  alternative.condition_count = static_cast<std::uint8_t>(conditions.size());
  for (const Condition& condition : conditions) {
    conditions_.push_back(condition);
  }
  alternatives->push_back(alternative);
  return Status::Ok;
}

Status Model::validate() const noexcept {
  if (states_.empty()) {
    return Status::SchemaError;
  }

  for (const auto& entry : states_) {
    for (const auto& transition : entry.second.transitions) {
      if (triggers_.find(transition.first) == triggers_.end()) {
        return Status::UnknownTrigger;
      }
      if (transition.second.empty()) {
        return Status::SchemaError;  // a trigger listed with no outcome at all
      }
      for (const Alternative& alternative : transition.second) {
        if (!has_state(alternative.target)) {
          return Status::UnknownState;
        }
        const std::size_t last = static_cast<std::size_t>(alternative.first_condition) +
                                 alternative.condition_count;
        if (last > conditions_.size()) {
          return Status::SchemaError;  // dangling slice of the condition pool
        }
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

TriggerId Model::find_trigger_for_channel(StringView channel) const noexcept {
  Channel key;
  if (!assign_checked(key, channel)) {
    return kNoTrigger;
  }
  const auto it = channel_index_.find(key);
  return (it == channel_index_.end()) ? kNoTrigger : it->second;
}

const char* Model::state_name(StateId id) const noexcept {
  const StateNode* node = state(id);
  return (node == nullptr) ? kInvalid : node->name.c_str();
}

const char* Model::trigger_name(TriggerId id) const noexcept {
  const TriggerDef* def = trigger(id);
  return (def == nullptr) ? kInvalid : def->name.c_str();
}

bool Model::accepts(StateId from, TriggerId trigger) const noexcept {
  const StateNode* node = state(from);
  return node != nullptr && node->transitions.find(trigger) != node->transitions.end();
}

Decision Model::evaluate(StateId from, TriggerId trigger, const Args& args,
                         StateId& target) const noexcept {
  target = kNoState;

  const StateNode* node = state(from);
  if (node == nullptr) {
    return Decision::NoTransition;
  }
  const auto it = node->transitions.find(trigger);
  if (it == node->transitions.end()) {
    return Decision::NoTransition;
  }

  // In order, first match wins.  An unguarded alternative always matches, which
  // is what makes it a fallback.
  for (const Alternative& alternative : it->second) {
    bool holds = true;
    for (std::uint8_t i = 0; holds && i < alternative.condition_count; ++i) {
      const std::size_t index = static_cast<std::size_t>(alternative.first_condition) + i;
      if (index >= conditions_.size()) {
        holds = false;  // cannot happen after validate(); refuse rather than read on
        break;
      }
      holds = conditions_[index].evaluate(args);  // conditions are ANDed
    }
    if (holds) {
      target = alternative.target;
      return Decision::Accepted;
    }
  }
  return Decision::GuardRejected;
}

StateId Model::target_of(StateId from, TriggerId trigger, const Args& args) const noexcept {
  StateId target = kNoState;
  evaluate(from, trigger, args, target);
  return target;
}

}  // namespace fms
