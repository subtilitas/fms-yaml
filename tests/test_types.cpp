// SPDX-License-Identifier: MIT
//
// assign_checked and append_clipped are the two places this library hands a
// pointer and a length to an ETL string, and both are reached with a
// default-constructed StringView: MemoryPort::inject takes its arguments
// parameter by default, and a machine that publishes no argument text passes
// one through.  Such a view has a null data() and a zero size(), and
// assign(nullptr, 0) passes null to a parameter declared never to be null - ETL
// forwards the pair to memmove, which the sanitizers report and which is
// undefined however benign it looks.
//
// The cases below assert the behaviour rather than the undefinedness, because a
// plain build cannot see the latter.  The sanitizers workflow is what catches a
// regression here; these say what the functions are supposed to do with nothing
// to copy, so the guard cannot be removed as dead code.
#include <doctest/doctest.h>

#include <etl/string.h>

#include "fms/types.hpp"

TEST_CASE("assign_checked accepts a view with no data") {
  const fms::StringView nothing;
  REQUIRE(nothing.data() == nullptr);
  REQUIRE(nothing.size() == 0);

  etl::string<31> destination("something");
  CHECK(fms::assign_checked(destination, nothing));
  CHECK(destination.empty());
}

TEST_CASE("append_clipped accepts a view with no data") {
  const fms::StringView nothing;
  etl::string<31> destination("kept");

  fms::append_clipped(destination, nothing);
  CHECK(destination == "kept");
}

TEST_CASE("append_clipped stops at the destination's capacity") {
  etl::string<7> destination;
  fms::append_clipped(destination, fms::StringView("0123456789", 10));
  CHECK(destination.size() == destination.max_size());

  // Full, so there is nothing left to take and the source pointer is never
  // read - the same path the empty view takes.
  fms::append_clipped(destination, fms::StringView("more", 4));
  CHECK(destination.size() == destination.max_size());
}
