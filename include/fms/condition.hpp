// SPDX-License-Identifier: MIT
//
// A guard, in the only form this project allows: one comparison against one
// trigger argument.
//
//     when: "pedal > 30"        ->  Condition{arg="pedal", op=Greater, number=30}
//     when: "mode == sport"     ->  Condition{arg="mode",  op=Equal,  literal="sport"}
//
// Guards are data, not code.  They are parsed once at load time - a condition
// that does not parse is a config error with a line number, not a run-time
// surprise - and evaluating one is a lookup plus a compare.
//
// Deliberate limits, so that a guard stays something you can read at a glance:
//   * one comparison per condition; list several under `when` to AND them, and
//     write several alternatives to OR them
//   * integers and strings only, no floats and no arithmetic
//   * ordering (< <= > >=) applies to integers; strings take only == and !=
//   * a missing argument makes the condition false, never an error
#ifndef FMS_CONDITION_HPP
#define FMS_CONDITION_HPP

#include <cstdint>

#include "fms/args.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms {

enum class CompareOp : std::uint8_t {
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
};

/// Static text for an operator, for diagnostics.  Never allocates.
const char* to_string(CompareOp op) noexcept;

struct Condition {
  Name         arg{};                     ///< which argument to look at
  CompareOp    op      = CompareOp::Equal;
  bool         numeric = false;           ///< compare as an integer, not as text
  std::int32_t number  = 0;               ///< the literal, when numeric
  Name         literal{};                 ///< the literal, when not numeric

  /// True when `args` satisfies this condition.  A missing or unparsable
  /// argument is false, never an error: guards decide, they do not fail.
  bool evaluate(const Args& args) const noexcept;
};

/// Parses "arg op literal", e.g. "pedal >= 30" or "mode == sport".  Whitespace
/// around the operator is optional, so "pedal>=30" works too.
///   Status::Ok             parsed
///   Status::SchemaError    missing operand, unknown operator, or an ordering
///                         comparison against a non-numeric literal
///   Status::NameTooLong    the argument name or the literal does not fit
Status parse_condition(StringView text, Condition& out) noexcept;

}  // namespace fms

#endif  // FMS_CONDITION_HPP
