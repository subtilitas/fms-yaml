// SPDX-License-Identifier: MIT
//
// Guards: one comparison against one argument, parsed at load time.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/condition.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

fms::Condition parsed(const char* text) {
  fms::Condition condition;
  REQUIRE(fms::parse_condition(sv(text), condition) == fms::Status::Ok);
  return condition;
}

/// `text` must be a literal: Args keeps views into it.
fms::Args with(const char* text) {
  fms::Args args;
  REQUIRE(args.parse(sv(text)) == fms::Status::Ok);
  return args;
}

}  // namespace

TEST_CASE("a numeric comparison is parsed into arg, operator and number") {
  const fms::Condition c = parsed("pedal >= 30");
  CHECK(std::strcmp(c.arg.c_str(), "pedal") == 0);
  CHECK(c.op == fms::CompareOp::GreaterEqual);
  CHECK(c.numeric);
  CHECK(c.number == 30);
}

TEST_CASE("whitespace around the operator is optional") {
  CHECK(parsed("pedal>30").number == 30);
  CHECK(parsed("  pedal  >  30  ").number == 30);
  CHECK(std::strcmp(parsed("pedal>30").arg.c_str(), "pedal") == 0);
}

TEST_CASE("every operator is recognised, longest match first") {
  CHECK(parsed("a == 1").op == fms::CompareOp::Equal);
  CHECK(parsed("a = 1").op == fms::CompareOp::Equal);  // a single '=' reads as equality
  CHECK(parsed("a != 1").op == fms::CompareOp::NotEqual);
  CHECK(parsed("a < 1").op == fms::CompareOp::Less);
  CHECK(parsed("a <= 1").op == fms::CompareOp::LessEqual);
  CHECK(parsed("a > 1").op == fms::CompareOp::Greater);
  CHECK(parsed("a >= 1").op == fms::CompareOp::GreaterEqual);
}

TEST_CASE("a non-numeric literal becomes a text comparison") {
  const fms::Condition c = parsed("mode == sport");
  CHECK_FALSE(c.numeric);
  CHECK(std::strcmp(c.literal.c_str(), "sport") == 0);
}

TEST_CASE("numeric comparisons evaluate as arithmetic") {
  const fms::Condition c = parsed("pedal > 30");

  CHECK(c.evaluate(with("pedal=31")));
  CHECK_FALSE(c.evaluate(with("pedal=30")));
  CHECK_FALSE(c.evaluate(with("pedal=-5")));
  CHECK(parsed("pedal <= 30").evaluate(with("pedal=30")));
  CHECK(parsed("errors == 0").evaluate(with("errors=0")));
  CHECK_FALSE(parsed("errors == 0").evaluate(with("errors=3")));
}

TEST_CASE("text comparisons evaluate as bytes") {
  CHECK(parsed("mode == sport").evaluate(with("mode=sport")));
  CHECK_FALSE(parsed("mode == sport").evaluate(with("mode=eco")));
  CHECK(parsed("mode != sport").evaluate(with("mode=eco")));
  CHECK_FALSE(parsed("mode != sport").evaluate(with("mode=sport")));
}

TEST_CASE("a missing or unparsable argument makes the guard false, never an error") {
  const fms::Condition numeric = parsed("pedal > 30");
  CHECK_FALSE(numeric.evaluate(with("")));            // not sent
  CHECK_FALSE(numeric.evaluate(with("other=99")));    // something else sent
  CHECK_FALSE(numeric.evaluate(with("pedal=fast")));  // sent, but not a number

  // Even '!=' is false without the argument: the guard did not hold, and that
  // is all a guard can say.
  CHECK_FALSE(parsed("mode != sport").evaluate(with("")));
}

TEST_CASE("unreadable guards are refused at parse time") {
  fms::Condition condition;

  CHECK(fms::parse_condition(sv(""), condition) == fms::Status::SchemaError);
  CHECK(fms::parse_condition(sv("pedal"), condition) == fms::Status::SchemaError);
  CHECK(fms::parse_condition(sv("pedal >"), condition) == fms::Status::SchemaError);
  CHECK(fms::parse_condition(sv("> 30"), condition) == fms::Status::SchemaError);

  // Ordering against text has no meaning, so it is rejected rather than guessed.
  CHECK(fms::parse_condition(sv("mode > sport"), condition) == fms::Status::SchemaError);
  CHECK(fms::parse_condition(sv("pedal > 30.5"), condition) == fms::Status::SchemaError);
}

TEST_CASE("operators print for diagnostics") {
  CHECK(std::strcmp(fms::to_string(fms::CompareOp::GreaterEqual), ">=") == 0);
  CHECK(std::strcmp(fms::to_string(fms::CompareOp::NotEqual), "!=") == 0);
}
