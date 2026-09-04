// SPDX-License-Identifier: MIT
//
// Trigger arguments: `pedal=42 mode=sport`, parsed into views over the caller's
// buffer.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "fms/args.hpp"
#include "fms/limits.hpp"

namespace {
fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }
}  // namespace

TEST_CASE("named values are parsed and read back") {
  fms::Args args;
  REQUIRE(args.parse(sv("pedal=42 mode=sport")) == fms::Status::Ok);

  CHECK(args.size() == 2);
  CHECK(args.has(sv("pedal")));
  CHECK(args.has(sv("mode")));
  CHECK_FALSE(args.has(sv("speed")));
  CHECK(args.text(sv("mode")) == sv("sport"));
  CHECK(args.text(sv("absent")).empty());
  CHECK(args.raw() == sv("pedal=42 mode=sport"));
}

TEST_CASE("extra whitespace is tolerated") {
  fms::Args args;
  REQUIRE(args.parse(sv("  pedal=42\t mode=sport  ")) == fms::Status::Ok);
  CHECK(args.size() == 2);
  CHECK(args.text(sv("pedal")) == sv("42"));
}

TEST_CASE("no arguments is not an error") {
  fms::Args args;
  CHECK(args.parse(fms::StringView{}) == fms::Status::Ok);
  CHECK(args.empty());
  CHECK(args.parse(sv("   ")) == fms::Status::Ok);
  CHECK(args.empty());
}

// Parses a table of key/value pairs in batches of FMS_MAX_ARGUMENTS, so a case
// tests the values it names rather than the number of slots the build happens
// to have.  Written out as one string, "a=1 b=2 c=3 d=4" is a capacity error
// wherever fewer than four fit, which made these cases assertions about the
// default configuration instead of about Args.
template <std::size_t N, typename Check>
void in_batches(const char* const (&keys)[N], const char* const (&values)[N], Check check) {
  for (std::size_t first = 0; first < N; first += fms::limits::kMaxArguments) {
    char text[256];
    int  used = 0;
    const std::size_t last = (first + fms::limits::kMaxArguments < N)
                                 ? first + fms::limits::kMaxArguments
                                 : N;
    for (std::size_t i = first; i < last; ++i) {
      used += std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used),
                            "%s%s=%s", (i == first) ? "" : " ", keys[i], values[i]);
    }

    fms::Args args;
    REQUIRE(args.parse(sv(text)) == fms::Status::Ok);
    for (std::size_t i = first; i < last; ++i) {
      check(args, i);
    }
  }
}

TEST_CASE("integers are parsed on demand") {
  static const char* const kKeys[]   = {"a", "b", "c", "d"};
  static const char* const kValues[] = {"42", "-7", "+3", "0"};
  static const std::int32_t kWanted[] = {42, -7, 3, 0};

  in_batches(kKeys, kValues, [](const fms::Args& args, std::size_t i) {
    std::int32_t value = 0;
    CHECK((args.as_int(sv(kKeys[i]), value) && value == kWanted[i]));
  });

  // What is not an integer, in batches for the same reason.
  static const char* const kBadKeys[]   = {"e", "f", "g"};
  static const char* const kBadValues[] = {"12.5", "fast", ""};

  in_batches(kBadKeys, kBadValues, [](const fms::Args& args, std::size_t i) {
    std::int32_t value = 0;
    CHECK_FALSE(args.as_int(sv(kBadKeys[i]), value));
  });

  fms::Args    args;
  std::int32_t value = 0;
  REQUIRE(args.parse(sv("a=42")) == fms::Status::Ok);
  CHECK_FALSE(args.as_int(sv("absent"), value));
}

TEST_CASE("integer parsing refuses to overflow") {
  std::int32_t value = 0;
  CHECK(fms::parse_int(sv("2147483647"), value));
  CHECK(value == 2147483647);
  CHECK_FALSE(fms::parse_int(sv("2147483648"), value));
  CHECK_FALSE(fms::parse_int(sv("99999999999999"), value));
  CHECK_FALSE(fms::parse_int(sv("-"), value));
  CHECK_FALSE(fms::parse_int(sv(""), value));
}

TEST_CASE("booleans accept the usual spellings") {
  static const char* const kKeys[]   = {"a", "b", "c", "d"};
  static const char* const kValues[] = {"true", "Off", "YES", "0"};
  static const bool        kWanted[] = {true, false, true, false};

  in_batches(kKeys, kValues, [](const fms::Args& args, std::size_t i) {
    bool value = false;
    CHECK((args.as_bool(sv(kKeys[i]), value) && value == kWanted[i]));
  });

  fms::Args args;
  bool      value = false;
  REQUIRE(args.parse(sv("a=true")) == fms::Status::Ok);

  REQUIRE(args.parse(sv("e=maybe")) == fms::Status::Ok);
  CHECK_FALSE(args.as_bool(sv("e"), value));
}

TEST_CASE("malformed arguments are reported, not guessed at") {
  fms::Args args;

  CHECK(args.parse(sv("pedal")) == fms::Status::ArgumentError);      // no '='
  CHECK(args.parse(sv("=42")) == fms::Status::ArgumentError);        // no key
  CHECK(args.parse(sv("a=1 a=2")) == fms::Status::DuplicateName);
  // One pair past whatever this build allows, rather than five against a
  // remembered four.
  char past_capacity[256];
  int  written = 0;
  for (std::size_t i = 0; i <= fms::limits::kMaxArguments; ++i) {
    written += std::snprintf(past_capacity + written,
                             sizeof(past_capacity) - static_cast<std::size_t>(written),
                             "%sk%zu=%zu", (i == 0) ? "" : " ", i, i);
  }
  CHECK(args.parse(sv(past_capacity)) == fms::Status::CapacityExceeded);

  char long_key[fms::limits::kMaxNameLength + 8];
  std::memset(long_key, 'x', sizeof(long_key));
  long_key[sizeof(long_key) - 1] = '\0';
  char text[128];
  (void)std::snprintf(text, sizeof(text), "%s=1", long_key);
  CHECK(args.parse(sv(text)) == fms::Status::NameTooLong);

  CHECK(args.empty());  // a failed parse leaves nothing behind
}

TEST_CASE("values may contain anything but whitespace") {
  static const char* const kKeys[]   = {"path", "expr"};
  static const char* const kValues[] = {"/car/state", "a==b"};

  // Only the first '=' splits, which is what expr is here for.
  in_batches(kKeys, kValues, [](const fms::Args& args, std::size_t i) {
    CHECK(args.text(sv(kKeys[i])) == sv(kValues[i]));
  });
}

TEST_CASE("the shared empty instance is usable and empty") {
  CHECK(fms::Args::none().empty());
  CHECK_FALSE(fms::Args::none().has(sv("anything")));
}
