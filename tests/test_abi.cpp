// SPDX-License-Identifier: MIT
//
// The capacity guard in fms/abi.hpp is a link-time check, so most of it cannot
// be tested from inside a program that already linked.  What can be tested here
// is the part that decides whether the guard has a hole: the tag has to list
// every capacity, in a fixed order, exactly as the macros spell it.
//
// tools/abi_guard_check.sh covers the other half - that a mismatched
// translation unit really does fail to link.
#include <cstddef>
#include <string>

#include <doctest/doctest.h>
#include <etl/array.h>

#include "fms/abi.hpp"
#include "fms/limits.hpp"

TEST_CASE("the abi tag records every capacity, in order") {
  // The order here is the order fms/abi.hpp pastes them in.  A capacity added
  // to limits.hpp and not to both places is a configuration two translation
  // units can differ in without the linker noticing.
  const etl::array<std::size_t, 11> capacities = {
      fms::limits::kMaxStates,
      fms::limits::kMaxTriggers,
      fms::limits::kMaxTransitionsPerState,
      fms::limits::kMaxAlternatives,
      fms::limits::kMaxConditionsPerGuard,
      fms::limits::kMaxConditions,
      fms::limits::kMaxArguments,
      fms::limits::kMaxNameLength,
      fms::limits::kMaxChannelLength,
      fms::limits::kMaxMessageLength,
      fms::limits::kMaxFindings,
  };

  std::string expected;
  for (const std::size_t capacity : capacities) {
    if (!expected.empty()) {
      expected += '_';
    }
    expected += std::to_string(capacity);
  }

  CHECK(std::string(fms::abi::tag()) == expected);
}

TEST_CASE("pinning the configuration resolves and does nothing") {
  // If this links, the tag this translation unit compiled with matches the one
  // fms_core defines - which is the whole claim.
  fms::abi::pin();
  CHECK(std::string(fms::abi::tag()).find_first_not_of("0123456789_") ==
        std::string::npos);
}
