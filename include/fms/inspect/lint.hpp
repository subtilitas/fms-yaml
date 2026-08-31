// SPDX-License-Identifier: MIT
//
// What is wrong with a machine that still loads.
//
// The loader answers one question: is this file a valid description?  It
// checks syntax, schema and every reference, and refuses anything it cannot
// turn into a Model.  A machine can pass all of that and still be wrong - a
// state nothing can reach, an alternative listed after the fallback that
// swallows it, a guard whose conditions cannot all hold at once.  None of
// those are file errors: they are the configuration saying something it did
// not mean.
//
// So this is a second pass, over the loaded Model rather than the text.  It
// never rejects and never changes anything; it reports.  Whether a finding
// should stop the program is the caller's decision, which is why severity is
// a property of the check and not of the loader.
//
// It is a read-only walk over fixed-capacity containers: no allocation, no
// exceptions, and safe to run on a target.
#ifndef FMS_INSPECT_LINT_HPP
#define FMS_INSPECT_LINT_HPP

#include <cstdint>

#include <etl/vector.h>

#include "fms/model.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms::lint {

/// What was found.  Each check answers one question the loader does not ask.
enum class Check : std::uint8_t {
  /// No sequence of triggers leads here from the initial state.  Dead
  /// behaviour: the state is described, and can never happen.
  UnreachableState,

  /// Nothing leads out of this state to a different one - either it lists no
  /// transitions at all, or every one of them is a self-transition.  Often
  /// deliberate (a terminal state), so a warning rather than an error.
  DeadEndState,

  /// The trigger is declared, but no state lists it.  Input on its channel is
  /// routed and then always refused, which reads as a bug in the sender.
  UnusedTrigger,

  /// An alternative after an unguarded one.  The unguarded alternative always
  /// holds, so nothing behind it can ever be evaluated.
  UnreachableAlternative,

  /// The conditions of one alternative cannot all hold at once - `pedal > 60`
  /// and `pedal < 5`, or a value required to be two different things.  The
  /// alternative is written down and can never be taken.
  ImpossibleGuard,

  /// An earlier alternative for the same trigger has exactly the same guard,
  /// so it wins every time this one would have.
  ShadowedAlternative,
};

enum class Severity : std::uint8_t {
  Warning,  ///< worth looking at; may well be intended
  Error,    ///< the configuration cannot mean this
};

/// One thing found, as coordinates into the model rather than as text - so a
/// caller can format it, count it, or ignore it by kind.  `state`, `trigger`
/// and `alternative` are filled in only where they apply.
struct Finding {
  Check         check       = Check::UnreachableState;
  StateId       state       = kNoState;
  TriggerId     trigger     = kNoTrigger;
  std::uint8_t  alternative = 0;  ///< 1-based position in the file's list
  /// Second party to the finding: the alternative that shadows this one, or
  /// the unguarded one that swallows it.  0 when there is none.
  std::uint8_t  other       = 0;
};

using Report = etl::vector<Finding, limits::kMaxFindings>;

/// Walks `model` and appends what it finds to `report`.
///
/// `initial` is the state the setup starts in; reachability has no meaning
/// without it, so pass `kNoState` when looking at a machine file on its own
/// and that one check is skipped.
///
///   Status::Ok               the walk completed
///   Status::CapacityExceeded more than FMS_MAX_FINDINGS findings; those that
///                            fit are in the report, and the machine has
///                            bigger problems than the ones that did not
Status analyse(const Model& model, StateId initial, Report& report) noexcept;

/// How seriously to take a check.  A property of the kind of finding, not of
/// the machine it was found in.
Severity severity_of(Check check) noexcept;

/// Stable slug for a check, e.g. "unreachable-state".  Static, never null.
const char* to_string(Check check) noexcept;
const char* to_string(Severity severity) noexcept;

/// True when the report contains at least one Error - the question a caller
/// deciding on an exit code is actually asking.
bool has_errors(const Report& report) noexcept;

/// One sentence about a finding, with the names resolved out of the model.
/// Clipped to the capacity of `out` rather than allocating.
void describe(const Model& model, const Finding& finding, Message& out) noexcept;

}  // namespace fms::lint

#endif  // FMS_INSPECT_LINT_HPP
