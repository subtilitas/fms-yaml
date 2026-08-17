// SPDX-License-Identifier: MIT
#include "fms/condition.hpp"

namespace fms {
namespace {

constexpr bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

StringView trim(StringView text) noexcept {
  std::size_t begin = 0;
  std::size_t end   = text.size();
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return StringView(text.data() + begin, end - begin);
}

/// Finds the operator: the longest match wins, so "<=" is not read as "<".
bool find_operator(StringView text, std::size_t& position, std::size_t& length,
                   CompareOp& op) noexcept {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c    = text[i];
    const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

    if (c == '=' && next == '=') {
      position = i; length = 2; op = CompareOp::Equal;        return true;
    }
    if (c == '!' && next == '=') {
      position = i; length = 2; op = CompareOp::NotEqual;     return true;
    }
    if (c == '<' && next == '=') {
      position = i; length = 2; op = CompareOp::LessEqual;    return true;
    }
    if (c == '>' && next == '=') {
      position = i; length = 2; op = CompareOp::GreaterEqual; return true;
    }
    if (c == '<') {
      position = i; length = 1; op = CompareOp::Less;         return true;
    }
    if (c == '>') {
      position = i; length = 1; op = CompareOp::Greater;      return true;
    }
    if (c == '=') {  // a single '=' reads as equality, which is what people write
      position = i; length = 1; op = CompareOp::Equal;        return true;
    }
  }
  return false;
}

bool is_ordering(CompareOp op) noexcept {
  return op == CompareOp::Less || op == CompareOp::LessEqual || op == CompareOp::Greater ||
         op == CompareOp::GreaterEqual;
}

}  // namespace

const char* to_string(CompareOp op) noexcept {
  switch (op) {
    case CompareOp::Equal:        return "==";
    case CompareOp::NotEqual:     return "!=";
    case CompareOp::Less:         return "<";
    case CompareOp::LessEqual:    return "<=";
    case CompareOp::Greater:      return ">";
    case CompareOp::GreaterEqual: return ">=";
  }
  return "?";
}

Status parse_condition(StringView text, Condition& out) noexcept {
  out = Condition{};

  const StringView expression = trim(text);
  if (expression.empty()) {
    return Status::SchemaError;
  }

  std::size_t position = 0;
  std::size_t length   = 0;
  CompareOp   op       = CompareOp::Equal;
  if (!find_operator(expression, position, length, op)) {
    return Status::SchemaError;  // no operator: not a comparison
  }

  const StringView left  = trim(StringView(expression.data(), position));
  const StringView right = trim(StringView(expression.data() + position + length,
                                           expression.size() - position - length));
  if (left.empty() || right.empty()) {
    return Status::SchemaError;
  }

  if (!assign_checked(out.arg, left)) {
    return Status::NameTooLong;
  }
  out.op = op;

  std::int32_t number = 0;
  if (parse_int(right, number)) {
    out.numeric = true;
    out.number  = number;
    return Status::Ok;
  }

  // A non-numeric literal can only be compared for equality: "mode > sport"
  // has no meaning, and silently guessing one would be worse than refusing.
  if (is_ordering(op)) {
    return Status::SchemaError;
  }
  if (!assign_checked(out.literal, right)) {
    return Status::NameTooLong;
  }
  out.numeric = false;
  return Status::Ok;
}

bool Condition::evaluate(const Args& args) const noexcept {
  const StringView value = args.text(view(arg));
  if (value.empty()) {
    return false;  // the argument was not sent: the guard simply does not hold
  }

  if (numeric) {
    std::int32_t actual = 0;
    if (!parse_int(value, actual)) {
      return false;  // sent, but not a number: same answer
    }
    switch (op) {
      case CompareOp::Equal:        return actual == number;
      case CompareOp::NotEqual:     return actual != number;
      case CompareOp::Less:         return actual < number;
      case CompareOp::LessEqual:    return actual <= number;
      case CompareOp::Greater:      return actual > number;
      case CompareOp::GreaterEqual: return actual >= number;
    }
    return false;
  }

  const bool same = (value == view(literal));
  return (op == CompareOp::NotEqual) ? !same : same;
}

}  // namespace fms
