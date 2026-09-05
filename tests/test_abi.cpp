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
#include <etl/flat_map.h>
#include <etl/string.h>
#include <etl/vector.h>

#include "fms/abi.hpp"
#include "fms/limits.hpp"

TEST_CASE("the etl layout constants describe the containers, not the build") {
  // Measured at a fixed small capacity on purpose: the numbers have to move
  // when ETL changes what a container costs and stay put when this build's
  // capacities change, because the capacities are in FMS_ABI_TAG already.  If
  // they tracked the capacities, every tuned build would refuse every other
  // one for a difference the capacity guard already covers.
  CHECK(fms::abi::etl_layout::vector == sizeof(etl::vector<char, 4>));
  CHECK(fms::abi::etl_layout::flat_map == sizeof(etl::flat_map<char, char, 4>));
  CHECK(fms::abi::etl_layout::string == sizeof(etl::string<7>));

  // A container with more room is a bigger object; the probe's numbers are not.
  CHECK(sizeof(etl::vector<char, 64>) > fms::abi::etl_layout::vector);
}

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
