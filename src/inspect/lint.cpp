// SPDX-License-Identifier: MIT
#include "fms/inspect/lint.hpp"

#include <etl/array.h>
#include <etl/vector.h>

#include "text.hpp"

namespace fms::lint {
namespace {

using inspect::append_condition;
using inspect::append_int;
using inspect::cstr;

/// Widened bounds for the interval fold below, so that `x > INT32_MAX` and
/// `x < INT32_MIN` narrow to an empty range instead of wrapping into a wide
/// one - which would turn a guard that can never hold into one that looks fine.
constexpr std::int64_t kIntMin = -2147483647LL - 1;
constexpr std::int64_t kIntMax = 2147483647LL;

/// Appends a finding, or reports that the report is full.  A machine that
/// produces more than FMS_MAX_FINDINGS findings has problems the ones that did
/// not fit would not have added to.
bool record(Report& report, const Finding& finding) noexcept {
  if (report.full()) {
    return false;
  }
  report.push_back(finding);
  return true;
}

// ---------------------------------------------------------------------------
// reachability
// ---------------------------------------------------------------------------

/// Depth-first from the initial state over every alternative of every
/// transition - guards are ignored on purpose.  "Reachable" here means the
/// configuration provides a path, not that any particular argument would take
/// it; a guard that happens never to hold is the ImpossibleGuard check's
/// business, and conflating the two would report one mistake twice.
bool check_reachability(const Model& model, StateId initial, Report& report) noexcept {
  if (initial == kNoState || !model.has_state(initial)) {
    return true;  // no setup to start from: the question does not arise
  }

  // State ids are dense - declare_state hands out states_.size() - so a flat
  // array indexed by id is the whole bookkeeping.
  etl::array<bool, limits::kMaxStates>     reached{};
  etl::vector<StateId, limits::kMaxStates> pending;

  reached[initial] = true;
  pending.push_back(initial);

  while (!pending.empty()) {
    const StateId from = pending.back();
    pending.pop_back();

    const StateNode* node = model.state(from);
    if (node == nullptr) {
      continue;
    }
    for (const auto& transition : node->transitions) {
      for (const Alternative& alternative : transition.second) {
        const StateId target = alternative.target;
        if (target < limits::kMaxStates && !reached[target]) {
          reached[target] = true;
          pending.push_back(target);
        }
      }
    }
  }

  bool room = true;
  for (const auto& entry : model.states()) {
    const StateId id = entry.first;
    if (id < limits::kMaxStates && !reached[id]) {
      Finding finding;
      finding.check = Check::UnreachableState;
      finding.state = id;
      room          = record(report, finding) && room;
    }
  }
  return room;
}

/// A state nothing leaves.  Every transition it has, if any, comes back to
/// itself, so once the machine is here only a restart moves it.
bool check_dead_ends(const Model& model, Report& report) noexcept {
  bool room = true;
  for (const auto& entry : model.states()) {
    const StateId id = entry.first;

    bool leaves = false;
    for (const auto& transition : entry.second.transitions) {
      for (const Alternative& alternative : transition.second) {
        if (alternative.target != id) {
          leaves = true;
        }
      }
    }
    if (!leaves) {
      Finding finding;
      finding.check = Check::DeadEndState;
      finding.state = id;
      room          = record(report, finding) && room;
    }
  }
  return room;
}

/// A trigger declared in the machine file that no state lists.  It still owns
/// a channel, so input arrives, is routed, and is refused by every state it
/// could arrive in - which looks like a fault in the sender rather than in the
/// configuration that caused it.
bool check_unused_triggers(const Model& model, Report& report) noexcept {
  etl::array<bool, limits::kMaxTriggers> used{};

  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      if (transition.first < limits::kMaxTriggers) {
        used[transition.first] = true;
      }
    }
  }

  bool room = true;
  for (const auto& entry : model.triggers()) {
    const TriggerId id = entry.first;
    if (id < limits::kMaxTriggers && !used[id]) {
      Finding finding;
      finding.check   = Check::UnusedTrigger;
      finding.trigger = id;
      room            = record(report, finding) && room;
    }
  }
  return room;
}

// ---------------------------------------------------------------------------
// guards
// ---------------------------------------------------------------------------

bool same_condition(const Condition& left, const Condition& right) noexcept {
  if (!(left.arg == right.arg) || left.op != right.op || left.numeric != right.numeric) {
    return false;
  }
  return left.numeric ? (left.number == right.number) : (left.literal == right.literal);
}

/// True when every condition of `earlier` also appears in `later`.
///
/// Then `later` holding implies `earlier` holding, so the earlier alternative
/// is taken every time the later one would have been - and the later one is
/// dead.  Identical guards are the common case and fall out of this as the
/// equality it is; a strictly weaker earlier guard shadows just as completely,
/// and is easier to miss by eye.
bool implies(const Model& model, const Alternative& earlier, const Alternative& later) noexcept {
  const Model::ConditionPool& pool = model.conditions();

  for (std::uint8_t i = 0; i < earlier.condition_count; ++i) {
    const std::size_t index = static_cast<std::size_t>(earlier.first_condition) + i;
    if (index >= pool.size()) {
      return false;
    }
    bool found = false;
    for (std::uint8_t j = 0; j < later.condition_count && !found; ++j) {
      const std::size_t other = static_cast<std::size_t>(later.first_condition) + j;
      found = other < pool.size() && same_condition(pool[index], pool[other]);
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

/// What one guard says about one argument, with every condition that mentions
/// it folded together: a range, the values excluded from it by name, and any
/// text value it was required or forbidden to have.
struct ArgumentFacts {
  std::int64_t low  = kIntMin;
  std::int64_t high = kIntMax;

  etl::array<std::int32_t, limits::kMaxConditionsPerGuard> excluded{};
  std::size_t                                             excluded_count = 0;

  etl::array<const Name*, limits::kMaxConditionsPerGuard> forbidden{};
  std::size_t                                            forbidden_count = 0;

  bool        numeric_seen = false;  ///< at least one comparison against a number
  const Name* required     = nullptr;  ///< a text value it must have
  bool        contradicted = false;  ///< two requirements that already disagree
};

void fold_numeric(ArgumentFacts& facts, const Condition& condition) noexcept {
  facts.numeric_seen         = true;
  const std::int64_t literal = condition.number;

  switch (condition.op) {
    case CompareOp::Equal:
      facts.low  = (literal > facts.low) ? literal : facts.low;
      facts.high = (literal < facts.high) ? literal : facts.high;
      break;
    case CompareOp::NotEqual:
      if (facts.excluded_count < facts.excluded.size()) {
        facts.excluded[facts.excluded_count++] = condition.number;
      }
      break;
    case CompareOp::Less:
      facts.high = (literal - 1 < facts.high) ? literal - 1 : facts.high;
      break;
    case CompareOp::LessEqual:
      facts.high = (literal < facts.high) ? literal : facts.high;
      break;
    case CompareOp::Greater:
      facts.low = (literal + 1 > facts.low) ? literal + 1 : facts.low;
      break;
    case CompareOp::GreaterEqual:
      facts.low = (literal > facts.low) ? literal : facts.low;
      break;
  }
}

void fold_text(ArgumentFacts& facts, const Condition& condition) noexcept {
  if (condition.op == CompareOp::Equal) {
    if (facts.required != nullptr && !(*facts.required == condition.literal)) {
      facts.contradicted = true;  // required to be two different words
    }
    facts.required = &condition.literal;
    return;
  }
  if (condition.op == CompareOp::NotEqual && facts.forbidden_count < facts.forbidden.size()) {
    facts.forbidden[facts.forbidden_count++] = &condition.literal;
  }
  // An ordering comparison against a word cannot be parsed, so there is none.
}

/// Folds every condition in the guard that talks about `argument`.
ArgumentFacts fold(const Model::ConditionPool& pool, std::size_t begin, std::size_t count,
                   const Name& argument) noexcept {
  ArgumentFacts facts;

  for (std::size_t i = 0; i < count; ++i) {
    const Condition& condition = pool[begin + i];
    if (!(condition.arg == argument)) {
      continue;
    }
    if (condition.numeric) {
      fold_numeric(facts, condition);
    } else {
      fold_text(facts, condition);
    }
  }
  return facts;
}

/// How many of the excluded values actually fall inside the range that is left.
std::int64_t excluded_inside(const ArgumentFacts& facts) noexcept {
  std::int64_t distinct = 0;

  for (std::size_t i = 0; i < facts.excluded_count; ++i) {
    const std::int64_t value = facts.excluded[i];
    if (value < facts.low || value > facts.high) {
      continue;
    }
    bool duplicate = false;
    for (std::size_t j = 0; j < i && !duplicate; ++j) {
      duplicate = (facts.excluded[j] == facts.excluded[i]);
    }
    distinct += duplicate ? 0 : 1;
  }
  return distinct;
}

/// True when no value satisfies what was folded.
bool unsatisfiable(const ArgumentFacts& facts) noexcept {
  if (facts.contradicted || facts.low > facts.high) {
    return true;
  }

  if (facts.required != nullptr) {
    // A numeric comparison is false unless the value parses as an integer, and
    // a literal that parses as one is stored numeric - so a text literal never
    // does.  Requiring both is requiring a contradiction.
    if (facts.numeric_seen) {
      return true;
    }
    for (std::size_t i = 0; i < facts.forbidden_count; ++i) {
      if (*facts.forbidden[i] == *facts.required) {
        return true;  // required to be a word it is also forbidden to be
      }
    }
  }

  // Every value the range still allows has been excluded by name.
  const std::int64_t span = facts.high - facts.low + 1;
  return span <= static_cast<std::int64_t>(facts.excluded_count) && excluded_inside(facts) >= span;
}

/// Can the ANDed conditions of one alternative all hold at once?
///
/// Conditions on different arguments cannot contradict each other, so the
/// question is asked once per argument, over the facts folded above.
///
/// This reports only what it can prove.  A guard it calls possible may still
/// never hold in practice - that is the sender's business, not the file's.
bool guard_impossible(const Model& model, const Alternative& alternative) noexcept {
  const Model::ConditionPool& pool  = model.conditions();
  const std::size_t           begin = alternative.first_condition;
  const std::size_t           count = alternative.condition_count;

  if (begin + count > pool.size()) {
    return false;  // cannot happen after Model::validate(); do not read on
  }

  for (std::size_t i = 0; i < count; ++i) {
    const Name& argument = pool[begin + i].arg;

    // One fold per distinct argument: skip this one if an earlier condition
    // already folded it.
    bool done = false;
    for (std::size_t j = 0; j < i && !done; ++j) {
      done = (pool[begin + j].arg == argument);
    }
    if (!done && unsatisfiable(fold(pool, begin, count, argument))) {
      return true;
    }
  }
  return false;
}

/// The three things that can be wrong with one trigger's list of alternatives.
/// At most one finding per alternative, and the most fundamental one wins: an
/// alternative nothing can reach is not also reported for the guard it holds,
/// because fixing the reachability is what makes that guard matter again.
bool check_alternatives(const Model& model, StateId state, TriggerId trigger,
                        const Alternatives& alternatives, Report& report) noexcept {
  bool         room     = true;
  std::uint8_t fallback = 0;  // 1-based position of the first unguarded entry

  for (std::size_t i = 0; i < alternatives.size(); ++i) {
    const Alternative& alternative = alternatives[i];

    Finding finding;
    finding.state       = state;
    finding.trigger     = trigger;
    finding.alternative = static_cast<std::uint8_t>(i + 1);

    if (fallback != 0) {
      finding.check = Check::UnreachableAlternative;
      finding.other = fallback;
      room          = record(report, finding) && room;
      continue;
    }
    if (alternative.condition_count == 0) {
      fallback = static_cast<std::uint8_t>(i + 1);
      continue;
    }
    if (guard_impossible(model, alternative)) {
      finding.check = Check::ImpossibleGuard;
      room          = record(report, finding) && room;
      continue;
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (implies(model, alternatives[j], alternative)) {
        finding.check = Check::ShadowedAlternative;
        finding.other = static_cast<std::uint8_t>(j + 1);
        room          = record(report, finding) && room;
        break;
      }
    }
  }
  return room;
}

}  // namespace

// ---------------------------------------------------------------------------

Status analyse(const Model& model, StateId initial, Report& report) noexcept {
  bool room = check_reachability(model, initial, report);
  room      = check_dead_ends(model, report) && room;
  room      = check_unused_triggers(model, report) && room;

  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      room = check_alternatives(model, entry.first, transition.first, transition.second, report) &&
             room;
    }
  }

  return room ? Status::Ok : Status::CapacityExceeded;
}

Severity severity_of(Check check) noexcept {
  switch (check) {
    // Both of these can be exactly what was meant: a terminal state is a
    // dead end, and a trigger can be declared before the state that will
    // use it.  Worth saying once; not worth failing over.
    case Check::DeadEndState:
    case Check::UnusedTrigger:
      return Severity::Warning;

    // These describe configuration that cannot mean what it says: behaviour
    // that no input can reach.  Nothing is gained by tolerating them.
    case Check::UnreachableState:
    case Check::UnreachableAlternative:
    case Check::ImpossibleGuard:
    case Check::ShadowedAlternative:
      return Severity::Error;
  }
  return Severity::Error;
}

const char* to_string(Check check) noexcept {
  switch (check) {
    case Check::UnreachableState:       return "unreachable-state";
    case Check::DeadEndState:           return "dead-end-state";
    case Check::UnusedTrigger:          return "unused-trigger";
    case Check::UnreachableAlternative: return "unreachable-alternative";
    case Check::ImpossibleGuard:        return "impossible-guard";
    case Check::ShadowedAlternative:    return "shadowed-alternative";
  }
  return "unknown-check";
}

const char* to_string(Severity severity) noexcept {
  return (severity == Severity::Error) ? "error" : "warning";
}

bool has_errors(const Report& report) noexcept {
  for (const Finding& finding : report) {
    if (severity_of(finding.check) == Severity::Error) {
      return true;
    }
  }
  return false;
}

void describe(const Model& model, const Finding& finding, Message& out) noexcept {
  out.clear();

  switch (finding.check) {
    case Check::UnreachableState:
      append_clipped(out, cstr("state '"));
      append_clipped(out, cstr(model.state_name(finding.state)));
      append_clipped(out, cstr("' cannot be reached from the initial state"));
      return;

    case Check::DeadEndState:
      append_clipped(out, cstr("state '"));
      append_clipped(out, cstr(model.state_name(finding.state)));
      append_clipped(out, cstr("' has no transition to another state"));
      return;

    case Check::UnusedTrigger:
      append_clipped(out, cstr("trigger '"));
      append_clipped(out, cstr(model.trigger_name(finding.trigger)));
      append_clipped(out, cstr("' is declared but no state lists it"));
      return;

    default:
      break;
  }

  // The three that are about one alternative share a prefix: which state,
  // which trigger, which of its alternatives.
  append_clipped(out, cstr("state '"));
  append_clipped(out, cstr(model.state_name(finding.state)));
  append_clipped(out, cstr("', trigger '"));
  append_clipped(out, cstr(model.trigger_name(finding.trigger)));
  append_clipped(out, cstr("', alternative "));
  append_int(out, finding.alternative);

  switch (finding.check) {
    case Check::UnreachableAlternative:
      append_clipped(out, cstr(": alternative "));
      append_int(out, finding.other);
      append_clipped(out, cstr(" has no guard, so nothing after it is reached"));
      return;

    case Check::ImpossibleGuard:
      append_clipped(out, cstr(": the guard can never hold ("));
      {
        const Model::ConditionPool& pool  = model.conditions();
        const StateNode*            node  = model.state(finding.state);
        const Alternatives*         list  = nullptr;
        if (node != nullptr) {
          const auto it = node->transitions.find(finding.trigger);
          list = (it == node->transitions.end()) ? nullptr : &it->second;
        }
        if (list != nullptr && finding.alternative > 0 && finding.alternative <= list->size()) {
          const Alternative& alternative = (*list)[finding.alternative - 1U];
          for (std::uint8_t i = 0; i < alternative.condition_count; ++i) {
            const std::size_t index = static_cast<std::size_t>(alternative.first_condition) + i;
            if (index >= pool.size()) {
              break;
            }
            if (i > 0) {
              append_clipped(out, cstr(" and "));
            }
            append_condition(out, pool[index]);
          }
        }
      }
      append_clipped(out, cstr(")"));
      return;

    case Check::ShadowedAlternative:
      append_clipped(out, cstr(": alternative "));
      append_int(out, finding.other);
      append_clipped(out, cstr(" holds whenever this one would"));
      return;

    default:
      return;
  }
}

}  // namespace fms::lint
