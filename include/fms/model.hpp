// SPDX-License-Identifier: MIT
//
// The behaviour half of the configuration: states, triggers, and nothing else.
// No names of brokers, no endpoints, no starting state - those live in Setup,
// so this object describes what the machine *does*, not where it is deployed.
//
//   states_        flat_map<StateId, StateNode>      a state and its dependencies
//   StateNode
//     .name        Name
//     .transitions flat_map<TriggerId, StateId>      trigger -> next state
//   state_index_   flat_map<Name, StateId>           name resolution (load time)
//   triggers_      flat_map<TriggerId, TriggerDef>   a trigger and its dependencies
//   trigger_index_ flat_map<Name, TriggerId>
//   channel_index_ flat_map<Channel, TriggerId>      inbound routing
//
// A trigger maps to exactly one target state per state, so every table is a
// plain flat_map: a sorted vector plus a pool, binary-search lookup, no nodes,
// no hashing, no allocation.
#ifndef FMS_MODEL_HPP
#define FMS_MODEL_HPP

#include <etl/flat_map.h>

#include "fms/limits.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms {

/// A trigger and its dependencies: the name transitions refer to, and the
/// channel a port delivers it on.  The channel defaults to the name.
struct TriggerDef {
  Name    name{};
  Channel channel{};
};

/// A state and its dependencies: its name and its outgoing transitions.
struct StateNode {
  using TransitionMap = etl::flat_map<TriggerId, StateId, limits::kMaxTransitionsPerState>;

  Name          name{};
  TransitionMap transitions{};
};

class Model {
 public:
  using StateMap     = etl::flat_map<StateId, StateNode, limits::kMaxStates>;
  using StateIndex   = etl::flat_map<Name, StateId, limits::kMaxStates>;
  using TriggerMap   = etl::flat_map<TriggerId, TriggerDef, limits::kMaxTriggers>;
  using TriggerIndex = etl::flat_map<Name, TriggerId, limits::kMaxTriggers>;
  using ChannelIndex = etl::flat_map<Channel, TriggerId, limits::kMaxTriggers>;

  Model() noexcept = default;

  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  // ---- build phase (loader only) -----------------------------------------

  void   clear() noexcept;
  Status set_name(StringView name) noexcept;
  Status declare_state(StringView name, StateId& out_id) noexcept;

  /// An empty `channel` defaults to `name`.
  Status declare_trigger(StringView name, StringView channel, TriggerId& out_id) noexcept;
  Status add_transition(StateId from, TriggerId trigger, StateId target) noexcept;

  /// Cross-checks every reference inside the machine.  Called at the end of
  /// loading.  Note that the initial state is not checked here - it belongs to
  /// the setup, and is verified when the two are bound in StateMachine::init.
  Status validate() const noexcept;

  // ---- read-only run-time interface --------------------------------------

  /// Optional name of the machine definition itself, e.g. "car".
  const Name& name() const noexcept { return name_; }

  const StateMap&     states() const noexcept { return states_; }
  const TriggerMap&   triggers() const noexcept { return triggers_; }
  const ChannelIndex& channel_index() const noexcept { return channel_index_; }

  bool              has_state(StateId id) const noexcept;
  const StateNode*  state(StateId id) const noexcept;
  const TriggerDef* trigger(TriggerId id) const noexcept;

  StateId   find_state(StringView name) const noexcept;
  TriggerId find_trigger(StringView name) const noexcept;
  TriggerId find_trigger_for_channel(StringView channel) const noexcept;

  /// Name of a state / trigger, or "<invalid>".  Never allocates.
  const char* state_name(StateId id) const noexcept;
  const char* trigger_name(TriggerId id) const noexcept;

  /// The target of `trigger` in `from`, or kNoState if the state does not
  /// accept it.  This is the whole state machine, really.
  StateId target_of(StateId from, TriggerId trigger) const noexcept;

  std::size_t state_count() const noexcept { return states_.size(); }
  std::size_t trigger_count() const noexcept { return triggers_.size(); }

 private:
  Name name_{};

  StateMap     states_{};
  StateIndex   state_index_{};
  TriggerMap   triggers_{};
  TriggerIndex trigger_index_{};
  ChannelIndex channel_index_{};
};

}  // namespace fms

#endif  // FMS_MODEL_HPP
