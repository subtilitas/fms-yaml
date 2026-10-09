// SPDX-License-Identifier: MIT
#include "fms/model.hpp"

namespace fms {
namespace {
constexpr const char* kInvalid = "<invalid>";
constexpr const char* kSelf    = "~self";

/// In order, first match wins.  An unguarded alternative always matches, which
/// is what makes it a fallback.  Null when none holds.
const Alternative* first_holding(const Alternatives& alternatives,
                                 const Model::ConditionPool& pool, const Args& args) noexcept {
  for (const Alternative& alternative : alternatives) {
    bool holds = true;
    for (std::uint8_t i = 0; holds && i < alternative.condition_count; ++i) {
      const std::size_t index = static_cast<std::size_t>(alternative.first_condition) + i;
      if (index >= pool.size()) {
        holds = false;  // cannot happen after validate(); refuse rather than read on
        break;
      }
      holds = pool[index].evaluate(args);  // conditions are ANDed
    }
    if (holds) {
      return &alternative;
    }
  }
  return nullptr;
}
}  // namespace

void Model::clear() noexcept {
  name_.clear();
  states_.clear();
  state_index_.clear();
  triggers_.clear();
  trigger_index_.clear();
  channel_index_.clear();
  conditions_.clear();
  groups_.clear();
  group_index_.clear();
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
  if (state_index_.find(key) != state_index_.end() ||
      group_index_.find(key) != group_index_.end()) {
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

Status Model::declare_group(StringView name, GroupId& out_id) noexcept {
  out_id = kNoGroup;

  if (name.empty()) {
    return Status::InvalidArgument;
  }
  if (groups_.full() || group_index_.full()) {
    return Status::CapacityExceeded;
  }

  Name key;
  if (!assign_checked(key, name)) {
    return Status::NameTooLong;
  }
  // One namespace with the states: a diagram draws a group as a box named like
  // a state, and two boxes with one name are one box.
  if (group_index_.find(key) != group_index_.end() ||
      state_index_.find(key) != state_index_.end()) {
    return Status::DuplicateName;
  }

  const GroupId id = static_cast<GroupId>(groups_.size());

  GroupNode node;
  node.name = key;
  groups_.insert(GroupMap::value_type(id, node));
  group_index_.insert(GroupIndex::value_type(key, id));

  out_id = id;
  return Status::Ok;
}

Status Model::add_to_group(GroupId group, StateId state) noexcept {
  if (groups_.find(group) == groups_.end()) {
    return Status::InvalidArgument;
  }
  const auto it = states_.find(state);
  if (it == states_.end()) {
    return Status::UnknownState;
  }
  if (it->second.group != kNoGroup) {
    return Status::DuplicateName;  // a state belongs to at most one group
  }
  it->second.group = group;
  return Status::Ok;
}

bool Model::is_target(StateId target) const noexcept {
  return target == kSelfState || has_state(target);
}

Status Model::append(TransitionMap& transitions, TriggerId trigger, StateId target,
                     const ConditionList& conditions) noexcept {
  if (triggers_.find(trigger) == triggers_.end()) {
    return Status::UnknownTrigger;
  }
  if (!is_target(target)) {
    return Status::UnknownState;
  }
  if (conditions_.size() + conditions.size() > conditions_.max_size()) {
    return Status::CapacityExceeded;
  }

  auto it = transitions.find(trigger);
  if (it == transitions.end()) {
    if (transitions.full()) {
      return Status::CapacityExceeded;  // no room for another trigger here
    }
    it = transitions.insert(TransitionMap::value_type(trigger, Alternatives{})).first;
  }
  Alternatives& alternatives = it->second;
  if (alternatives.full()) {
    return Status::CapacityExceeded;  // no room for another alternative
  }

  Alternative alternative;
  alternative.target          = target;
  alternative.first_condition = static_cast<std::uint16_t>(conditions_.size());
  alternative.condition_count = static_cast<std::uint8_t>(conditions.size());
  for (const Condition& condition : conditions) {
    conditions_.push_back(condition);
  }
  alternatives.push_back(alternative);
  return Status::Ok;
}

Status Model::add_transition(StateId from, TriggerId trigger, StateId target) noexcept {
  return add_transition(from, trigger, target, ConditionList{});
}

Status Model::add_transition(StateId from, TriggerId trigger, StateId target,
                             const ConditionList& conditions) noexcept {
  const auto it = states_.find(from);
  if (it == states_.end()) {
    return Status::UnknownState;
  }
  return append(it->second.transitions, trigger, target, conditions);
}

Status Model::add_group_transition(GroupId group, TriggerId trigger, StateId target) noexcept {
  return add_group_transition(group, trigger, target, ConditionList{});
}

Status Model::add_group_transition(GroupId group, TriggerId trigger, StateId target,
                                   const ConditionList& conditions) noexcept {
  const auto it = groups_.find(group);
  if (it == groups_.end()) {
    return Status::InvalidArgument;
  }
  return append(it->second.transitions, trigger, target, conditions);
}

Status Model::validate(const TransitionMap& transitions) const noexcept {
  for (const auto& transition : transitions) {
    if (triggers_.find(transition.first) == triggers_.end()) {
      return Status::UnknownTrigger;
    }
    if (transition.second.empty()) {
      return Status::SchemaError;  // a trigger listed with no outcome at all
    }
    for (const Alternative& alternative : transition.second) {
      if (!is_target(alternative.target)) {
        return Status::UnknownState;
      }
      const std::size_t last = static_cast<std::size_t>(alternative.first_condition) +
                               alternative.condition_count;
      if (last > conditions_.size()) {
        return Status::SchemaError;  // dangling slice of the condition pool
      }
    }
  }
  return Status::Ok;
}

Status Model::validate() const noexcept {
  if (states_.empty()) {
    return Status::SchemaError;
  }

  for (const auto& entry : states_) {
    if (entry.second.group != kNoGroup && groups_.find(entry.second.group) == groups_.end()) {
      return Status::SchemaError;  // a member of a group that does not exist
    }
    const Status status = validate(entry.second.transitions);
    if (!is_ok(status)) {
      return status;
    }
  }
  for (const auto& entry : groups_) {
    const Status status = validate(entry.second.transitions);
    if (!is_ok(status)) {
      return status;
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

const GroupNode* Model::group(GroupId id) const noexcept {
  const auto it = groups_.find(id);
  return (it == groups_.end()) ? nullptr : &it->second;
}

GroupId Model::group_of(StateId state) const noexcept {
  const StateNode* node = this->state(state);
  return (node == nullptr) ? kNoGroup : node->group;
}

GroupId Model::find_group(StringView name) const noexcept {
  Name key;
  if (!assign_checked(key, name)) {
    return kNoGroup;
  }
  const auto it = group_index_.find(key);
  return (it == group_index_.end()) ? kNoGroup : it->second;
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
  if (id == kSelfState) {
    return kSelf;
  }
  const StateNode* node = state(id);
  return (node == nullptr) ? kInvalid : node->name.c_str();
}

const char* Model::trigger_name(TriggerId id) const noexcept {
  const TriggerDef* def = trigger(id);
  return (def == nullptr) ? kInvalid : def->name.c_str();
}

const char* Model::group_name(GroupId id) const noexcept {
  const GroupNode* node = group(id);
  return (node == nullptr) ? kInvalid : node->name.c_str();
}

bool Model::accepts(StateId from, TriggerId trigger) const noexcept {
  const StateNode* node = state(from);
  if (node == nullptr) {
    return false;
  }
  if (node->transitions.find(trigger) != node->transitions.end()) {
    return true;
  }
  const GroupNode* shared = group(node->group);
  return shared != nullptr && shared->transitions.find(trigger) != shared->transitions.end();
}

Decision Model::evaluate(StateId from, TriggerId trigger, const Args& args,
                         StateId& target) const noexcept {
  GroupId via = kNoGroup;
  return evaluate(from, trigger, args, target, via);
}

Decision Model::evaluate(StateId from, TriggerId trigger, const Args& args, StateId& target,
                         GroupId& via) const noexcept {
  target = kNoState;
  via    = kNoGroup;

  const StateNode* node = state(from);
  if (node == nullptr) {
    return Decision::NoTransition;
  }

  // The state first, then its group: a member refines or replaces what the
  // group says, and the group is asked only when the member had no answer.
  bool listed = false;

  const auto own = node->transitions.find(trigger);
  if (own != node->transitions.end()) {
    listed = true;
    const Alternative* taken = first_holding(own->second, conditions_, args);
    if (taken != nullptr) {
      target = (taken->target == kSelfState) ? from : taken->target;
      return Decision::Accepted;
    }
  }

  const GroupNode* shared = group(node->group);
  if (shared != nullptr) {
    const auto inherited = shared->transitions.find(trigger);
    if (inherited != shared->transitions.end()) {
      listed = true;
      const Alternative* taken = first_holding(inherited->second, conditions_, args);
      if (taken != nullptr) {
        target = (taken->target == kSelfState) ? from : taken->target;
        via    = node->group;
        return Decision::Accepted;
      }
    }
  }
  return listed ? Decision::GuardRejected : Decision::NoTransition;
}

StateId Model::target_of(StateId from, TriggerId trigger, const Args& args) const noexcept {
  StateId target = kNoState;
  evaluate(from, trigger, args, target);
  return target;
}

}  // namespace fms
