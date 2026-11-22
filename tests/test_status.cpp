// SPDX-License-Identifier: MIT
//
// to_string is the only way a Status or a comparison operator reaches a human,
// and both are a switch over an enumeration.  An enumerator added without an
// arm compiles, falls through to the default, and becomes "unknown status" - a
// diagnostic that says nothing, discovered by whoever is holding the failing
// config at the time.
//
// So these walk every enumerator.  Adding one without its arm is then a failing
// test rather than a silent hole, which is the only reason this file exists:
// nothing here asserts behaviour the machine has, only that the machine can
// describe itself.
#include <doctest/doctest.h>

#include "fms/condition.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text); }

// Every enumerator of fms::Status, in declaration order.  Kept explicit rather
// than derived from a range: the point is that a new one has to be added here
// by hand, which is exactly the moment to notice its to_string arm is missing.
constexpr fms::Status kAllStatuses[] = {
    fms::Status::Ok,
    fms::Status::FileNotFound,
    fms::Status::FileNotReadable,
    fms::Status::ParseError,
    fms::Status::SchemaError,
    fms::Status::DuplicateName,
    fms::Status::UnknownState,
    fms::Status::UnknownTrigger,
    fms::Status::NameTooLong,
    fms::Status::ChannelTooLong,
    fms::Status::CapacityExceeded,
    fms::Status::NotInitialised,
    fms::Status::AlreadyInitialised,
    fms::Status::InvalidArgument,
    fms::Status::NoTransition,
    fms::Status::GuardRejected,
    fms::Status::ArgumentError,
    fms::Status::PortError,
    fms::Status::NotOpen,
    fms::Status::Timeout,
    fms::Status::EndOfInput,
};

constexpr fms::CompareOp kAllOps[] = {
    fms::CompareOp::Equal,        fms::CompareOp::NotEqual, fms::CompareOp::Less,
    fms::CompareOp::LessEqual,    fms::CompareOp::Greater,  fms::CompareOp::GreaterEqual,
};

}  // namespace

TEST_CASE("every Status describes itself, and no two describe themselves alike") {
  for (const fms::Status status : kAllStatuses) {
    const char* text = fms::to_string(status);
    REQUIRE(text != nullptr);
    CHECK(!sv(text).empty());
    // The default arm.  Reaching it here means an enumerator has no case.
    CHECK(sv(text) != sv("unknown status"));
  }

  constexpr std::size_t count = sizeof(kAllStatuses) / sizeof(kAllStatuses[0]);
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      INFO("statuses ", i, " and ", j, " share a description");
      CHECK(sv(fms::to_string(kAllStatuses[i])) != sv(fms::to_string(kAllStatuses[j])));
    }
  }
}

TEST_CASE("a Status from nowhere still returns a string rather than falling off the end") {
  // The analyser is right that 200 names no enumerator - that is the point.
  // Status is backed by uint8_t, so the value is representable and the cast is
  // defined, and this is the only way to reach the switch's default arm.  A
  // caller formatting a corrupted status must still get something printable
  // back, never a null.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  const char* text = fms::to_string(static_cast<fms::Status>(200));
  REQUIRE(text != nullptr);
  CHECK(sv(text) == sv("unknown status"));
}

TEST_CASE("is_ok is true for exactly one status") {
  int ok = 0;
  for (const fms::Status status : kAllStatuses) {
    if (fms::is_ok(status)) {
      ++ok;
      CHECK(status == fms::Status::Ok);
    }
  }
  CHECK(ok == 1);
}

TEST_CASE("every comparison operator prints as it is written in a guard") {
  CHECK(sv(fms::to_string(fms::CompareOp::Equal)) == sv("=="));
  CHECK(sv(fms::to_string(fms::CompareOp::NotEqual)) == sv("!="));
  CHECK(sv(fms::to_string(fms::CompareOp::Less)) == sv("<"));
  CHECK(sv(fms::to_string(fms::CompareOp::LessEqual)) == sv("<="));
  CHECK(sv(fms::to_string(fms::CompareOp::Greater)) == sv(">"));
  CHECK(sv(fms::to_string(fms::CompareOp::GreaterEqual)) == sv(">="));

  for (const fms::CompareOp op : kAllOps) {
    CHECK(sv(fms::to_string(op)) != sv("?"));
  }
  // Same reasoning as the Status case above: the default arm has no other route.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK(sv(fms::to_string(static_cast<fms::CompareOp>(99))) == sv("?"));
}
