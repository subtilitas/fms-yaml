// SPDX-License-Identifier: MIT
//
// The machine description, built once from YAML and then read-only.
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

/// Where the machine talks, and how to reach the world.  Every field is opaque
/// to the core - it is the port that decides what an endpoint string means.
struct IoConfig {
  Channel state_channel{};  ///< the new state's name is published here on every change
  Channel error_channel{};  ///< rejected triggers are reported here
  Channel endpoint{};       ///< broker URI, device path, socket - port specific
  Name    identity{};       ///< client id, node name - port specific
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
  Status set_initial(StateId id) noexcept;

  /// Cross-checks every reference.  Called at the end of loading.
  Status validate() const noexcept;

  IoConfig& mutable_io() noexcept { return io_; }

  // ---- read-only run-time interface --------------------------------------

  const Name& name() const noexcept { return name_; }
  StateId     initial() const noexcept { return initial_; }

  const StateMap&     states() const noexcept { return states_; }
  const TriggerMap&   triggers() const noexcept { return triggers_; }
  const ChannelIndex& channel_index() const noexcept { return channel_index_; }
  const IoConfig&     io() const noexcept { return io_; }

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
  Name     name_{};
  StateId  initial_ = kNoState;
  IoConfig io_{};

  StateMap     states_{};
  StateIndex   state_index_{};
  TriggerMap   triggers_{};
  TriggerIndex trigger_index_{};
  ChannelIndex channel_index_{};
};

}  // namespace fms

#endif  // FMS_MODEL_HPP
