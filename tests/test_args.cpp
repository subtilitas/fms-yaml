// SPDX-License-Identifier: MIT
//
// Trigger arguments: `pedal=42 mode=sport`, parsed into views over the caller's
// buffer.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/args.hpp"

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

TEST_CASE("integers are parsed on demand") {
  fms::Args    args;
  std::int32_t value = 0;

  REQUIRE(args.parse(sv("a=42 b=-7 c=+3 d=0")) == fms::Status::Ok);
  CHECK((args.as_int(sv("a"), value) && value == 42));
  CHECK((args.as_int(sv("b"), value) && value == -7));
  CHECK((args.as_int(sv("c"), value) && value == 3));
  CHECK((args.as_int(sv("d"), value) && value == 0));

  REQUIRE(args.parse(sv("e=12.5 f=fast g=")) == fms::Status::Ok);
  CHECK_FALSE(args.as_int(sv("e"), value));  // 12.5 is not an integer
  CHECK_FALSE(args.as_int(sv("f"), value));
  CHECK_FALSE(args.as_int(sv("g"), value));  // empty value
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
  fms::Args args;
  bool      value = false;

  REQUIRE(args.parse(sv("a=true b=Off c=YES d=0")) == fms::Status::Ok);
  CHECK((args.as_bool(sv("a"), value) && value));
  CHECK((args.as_bool(sv("b"), value) && !value));
  CHECK((args.as_bool(sv("c"), value) && value));
  CHECK((args.as_bool(sv("d"), value) && !value));

  REQUIRE(args.parse(sv("e=maybe")) == fms::Status::Ok);
  CHECK_FALSE(args.as_bool(sv("e"), value));
}

TEST_CASE("malformed arguments are reported, not guessed at") {
  fms::Args args;

  CHECK(args.parse(sv("pedal")) == fms::Status::ArgumentError);      // no '='
  CHECK(args.parse(sv("=42")) == fms::Status::ArgumentError);        // no key
  CHECK(args.parse(sv("a=1 a=2")) == fms::Status::DuplicateName);
  CHECK(args.parse(sv("a=1 b=2 c=3 d=4 e=5")) == fms::Status::CapacityExceeded);

  char long_key[fms::limits::kMaxNameLength + 8];
  std::memset(long_key, 'x', sizeof(long_key));
  long_key[sizeof(long_key) - 1] = '\0';
  char text[128];
  (void)std::snprintf(text, sizeof(text), "%s=1", long_key);
  CHECK(args.parse(sv(text)) == fms::Status::NameTooLong);

  CHECK(args.empty());  // a failed parse leaves nothing behind
}

TEST_CASE("values may contain anything but whitespace") {
  fms::Args args;
  REQUIRE(args.parse(sv("path=/car/state topic=a/b/c expr=a==b")) == fms::Status::Ok);
  CHECK(args.text(sv("path")) == sv("/car/state"));
  CHECK(args.text(sv("expr")) == sv("a==b"));  // only the first '=' splits
}

TEST_CASE("the shared empty instance is usable and empty") {
  CHECK(fms::Args::none().empty());
  CHECK_FALSE(fms::Args::none().has(sv("anything")));
}
