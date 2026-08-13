// SPDX-License-Identifier: MIT
//
// The machine description, built once from YAML and then read-only.
//
//   states_        flat_map<StateId, StateNode>        a state and its dependencies
//   StateNode
//     .name        Name
//     .transitions flat_map<TriggerId, StateId>        trigger -> next state
//   state_index_   flat_map<Name, StateId>             name resolution (load time)
//   triggers_      flat_map<TriggerId, TriggerDef>     a trigger and its dependencies
//   trigger_index_ flat_map<Name, TriggerId>
//   topic_index_   flat_map<Topic, TriggerId>          inbound MQTT routing
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

/// A trigger and its dependencies: the name transitions refer to, and the MQTT
/// topic a subsystem publishes on to raise it.
struct TriggerDef {
  Name  name{};
  Topic topic{};
};

/// A state and its dependencies: its name and its outgoing transitions.
struct StateNode {
  using TransitionMap = etl::flat_map<TriggerId, StateId, limits::kMaxTransitionsPerState>;

  Name          name{};
  TransitionMap transitions{};
};

/// Broker settings, also read from the YAML file.
struct MqttConfig {
  Topic         broker{"tcp://localhost:1883"};
  Name          client_id{"fms"};
  std::uint16_t keep_alive_s      = 30;
  bool          clean_session     = true;
  std::uint8_t  qos               = 1;
  std::uint32_t connect_timeout_ms = 5000;

  Topic state_topic{};   ///< the new state name is published here on every change
  Topic error_topic{};   ///< rejected triggers are reported here
  bool  retain_state = true;
};

class Model {
 public:
  using StateMap     = etl::flat_map<StateId, StateNode, limits::kMaxStates>;
  using StateIndex   = etl::flat_map<Name, StateId, limits::kMaxStates>;
  using TriggerMap   = etl::flat_map<TriggerId, TriggerDef, limits::kMaxTriggers>;
  using TriggerIndex = etl::flat_map<Name, TriggerId, limits::kMaxTriggers>;
  using TopicIndex   = etl::flat_map<Topic, TriggerId, limits::kMaxTriggers>;

  Model() noexcept = default;

  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  // ---- build phase (loader only) -----------------------------------------

  void   clear() noexcept;
  Status set_name(StringView name) noexcept;
  Status declare_state(StringView name, StateId& out_id) noexcept;
  Status declare_trigger(StringView name, StringView topic, TriggerId& out_id) noexcept;
  Status add_transition(StateId from, TriggerId trigger, StateId target) noexcept;
  Status set_initial(StateId id) noexcept;

  /// Cross-checks every reference.  Called at the end of loading.
  Status validate() const noexcept;

  MqttConfig& mutable_mqtt() noexcept { return mqtt_; }

  // ---- read-only run-time interface --------------------------------------

  const Name& name() const noexcept { return name_; }
  StateId     initial() const noexcept { return initial_; }

  const StateMap&   states() const noexcept { return states_; }
  const TriggerMap& triggers() const noexcept { return triggers_; }
  const TopicIndex& topic_index() const noexcept { return topic_index_; }
  const MqttConfig& mqtt() const noexcept { return mqtt_; }

  bool              has_state(StateId id) const noexcept;
  const StateNode*  state(StateId id) const noexcept;
  const TriggerDef* trigger(TriggerId id) const noexcept;

  StateId   find_state(StringView name) const noexcept;
  TriggerId find_trigger(StringView name) const noexcept;
  TriggerId find_trigger_for_topic(StringView topic) const noexcept;

  /// Name of a state / trigger, or "<invalid>".  Never allocates.
  const char* state_name(StateId id) const noexcept;
  const char* trigger_name(TriggerId id) const noexcept;

  /// The target of `trigger` in `from`, or kNoState if the state does not
  /// accept it.  This is the whole state machine, really.
  StateId target_of(StateId from, TriggerId trigger) const noexcept;

  std::size_t state_count() const noexcept { return states_.size(); }
  std::size_t trigger_count() const noexcept { return triggers_.size(); }

 private:
  Name       name_{};
  StateId    initial_ = kNoState;
  MqttConfig mqtt_{};

  StateMap     states_{};
  StateIndex   state_index_{};
  TriggerMap   triggers_{};
  TriggerIndex trigger_index_{};
  TopicIndex   topic_index_{};
};

}  // namespace fms

#endif  // FMS_MODEL_HPP
