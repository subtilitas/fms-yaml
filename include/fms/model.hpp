// SPDX-License-Identifier: MIT
//
// The behaviour half of the configuration: states, triggers and guards, and
// nothing else.  No endpoints, no starting state - those live in Setup.
//
//   states_        flat_map<StateId, StateNode>        a state and its dependencies
//   StateNode
//     .name        Name
//     .transitions flat_map<TriggerId, Alternatives>   trigger -> ordered choices
//       Alternative { first_condition, condition_count, target }
//   conditions_    vector<Condition>                   machine-wide guard pool
//   state_index_   flat_map<Name, StateId>             name resolution (load time)
//   triggers_      flat_map<TriggerId, TriggerDef>     a trigger and its dependencies
//   trigger_index_ flat_map<Name, TriggerId>
//   channel_index_ flat_map<Channel, TriggerId>        inbound routing
//
// A trigger still has exactly one entry per state, so the tables stay plain
// flat_maps - binary search over contiguous storage, no nodes, no hashing, no
// allocation.  What changed with guards is that the entry is now a short ordered
// list of alternatives instead of a single target.
//
// Conditions are interned in one pool and referenced by index, so an alternative
// costs six bytes rather than embedding two fixed-size strings.  Without that,
// a fully populated Model would be hundreds of kilobytes.
#ifndef FMS_MODEL_HPP
#define FMS_MODEL_HPP

#include <etl/flat_map.h>
#include <etl/vector.h>

#include "fms/args.hpp"
#include "fms/condition.hpp"
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

/// One possible outcome of a trigger in a state.  `condition_count == 0` means
/// unguarded, which always holds - so an unguarded alternative is the fallback
/// and anything after it is unreachable.
struct Alternative {
  std::uint16_t first_condition = 0;
  std::uint8_t  condition_count = 0;
  StateId       target          = kNoState;
};

using Alternatives = etl::vector<Alternative, limits::kMaxAlternatives>;
using ConditionList = etl::vector<Condition, limits::kMaxConditionsPerGuard>;

/// A state and its dependencies: its name and its outgoing transitions.
struct StateNode {
  using TransitionMap = etl::flat_map<TriggerId, Alternatives, limits::kMaxTransitionsPerState>;

  Name          name{};
  TransitionMap transitions{};
};

/// Why a trigger was or was not taken.
enum class Decision : std::uint8_t {
  Accepted,       ///< an alternative held; `target` is where to go
  NoTransition,   ///< the state does not list this trigger at all
  GuardRejected,  ///< it lists it, but no alternative's guard held
};

class Model {
 public:
  using StateMap     = etl::flat_map<StateId, StateNode, limits::kMaxStates>;
  using StateIndex   = etl::flat_map<Name, StateId, limits::kMaxStates>;
  using TriggerMap   = etl::flat_map<TriggerId, TriggerDef, limits::kMaxTriggers>;
  using TriggerIndex = etl::flat_map<Name, TriggerId, limits::kMaxTriggers>;
  using ChannelIndex = etl::flat_map<Channel, TriggerId, limits::kMaxTriggers>;
  using ConditionPool = etl::vector<Condition, limits::kMaxConditions>;

  Model() noexcept = default;

  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  // ---- build phase (loader only) -----------------------------------------

  void   clear() noexcept;
  Status set_name(StringView name) noexcept;
  Status declare_state(StringView name, StateId& out_id) noexcept;

  /// An empty `channel` defaults to `name`.
  Status declare_trigger(StringView name, StringView channel, TriggerId& out_id) noexcept;

  /// Appends an unguarded alternative: this trigger always leads to `target`.
  Status add_transition(StateId from, TriggerId trigger, StateId target) noexcept;

  /// Appends a guarded alternative.  `conditions` are ANDed and copied into the
  /// machine's condition pool.  Alternatives are evaluated in the order added,
  /// which is the order they appear in the file.
  Status add_transition(StateId from, TriggerId trigger, StateId target,
                        const ConditionList& conditions) noexcept;

  /// Cross-checks every reference inside the machine.  Called at the end of
  /// loading.  The initial state is not checked here - it belongs to the setup,
  /// and is verified when the two are bound in StateMachine::init.
  Status validate() const noexcept;

  // ---- read-only run-time interface --------------------------------------

  /// Optional name of the machine definition itself, e.g. "car".
  const Name& name() const noexcept { return name_; }

  const StateMap&      states() const noexcept { return states_; }
  const TriggerMap&    triggers() const noexcept { return triggers_; }
  const ChannelIndex&  channel_index() const noexcept { return channel_index_; }
  const ConditionPool& conditions() const noexcept { return conditions_; }

  bool              has_state(StateId id) const noexcept;
  const StateNode*  state(StateId id) const noexcept;
  const TriggerDef* trigger(TriggerId id) const noexcept;

  StateId   find_state(StringView name) const noexcept;
  TriggerId find_trigger(StringView name) const noexcept;
  TriggerId find_trigger_for_channel(StringView channel) const noexcept;

  /// Name of a state / trigger, or "<invalid>".  Never allocates.
  const char* state_name(StateId id) const noexcept;
  const char* trigger_name(TriggerId id) const noexcept;

  /// True when `from` lists `trigger` at all, guards aside.
  bool accepts(StateId from, TriggerId trigger) const noexcept;

  /// The whole state machine, really: walk the alternatives in order, take the
  /// first whose guard holds.
  Decision evaluate(StateId from, TriggerId trigger, const Args& args,
                    StateId& target) const noexcept;

  /// Convenience: the target, or kNoState if the trigger is not accepted.
  StateId target_of(StateId from, TriggerId trigger,
                    const Args& args = Args::none()) const noexcept;

  std::size_t state_count() const noexcept { return states_.size(); }
  std::size_t trigger_count() const noexcept { return triggers_.size(); }
  std::size_t condition_count() const noexcept { return conditions_.size(); }

 private:
  Alternatives* alternatives_for(StateId from, TriggerId trigger) noexcept;

  Name name_{};

  StateMap      states_{};
  StateIndex    state_index_{};
  TriggerMap    triggers_{};
  TriggerIndex  trigger_index_{};
  ChannelIndex  channel_index_{};
  ConditionPool conditions_{};
};

}  // namespace fms

#endif  // FMS_MODEL_HPP
