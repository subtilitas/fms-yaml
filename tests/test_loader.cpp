// SPDX-License-Identifier: MIT
//
// The machine half of the configuration.  The loader is the exception firewall:
// every case here must come back as a Status with a useful diagnostic, never as
// a throw.  Setup-file cases live in test_setup.cpp.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

struct Loaded {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  fms::Status load(const char* yaml) {
    return fms::config::load_machine_string(yaml, model, diagnostics);
  }
};

constexpr const char* kMinimal = R"(
fsm:
  name: minimal
triggers:
  - {name: throttle, channel: "car/engine/throttle"}
  - {name: brake,    channel: "car/brakes/pressed"}
states:
  - name: standing
    transitions:
      throttle: driving
  - name: driving
    transitions:
      brake: standing
)";

}  // namespace

TEST_CASE("a valid machine produces a validated model") {
  Loaded fixture;
  INFO("diagnostic: ", fixture.diagnostics.message.c_str());
  REQUIRE(fixture.load(kMinimal) == fms::Status::Ok);

  CHECK(std::strcmp(fixture.model.name().c_str(), "minimal") == 0);
  CHECK(fixture.model.state_count() == 2);
  CHECK(fixture.model.trigger_count() == 2);
  CHECK(fixture.model.validate() == fms::Status::Ok);

  const fms::StateId   standing = fixture.model.find_state(sv("standing"));
  const fms::StateId   driving  = fixture.model.find_state(sv("driving"));
  const fms::TriggerId throttle = fixture.model.find_trigger(sv("throttle"));
  const fms::TriggerId brake    = fixture.model.find_trigger(sv("brake"));

  CHECK(fixture.model.target_of(standing, throttle) == driving);
  CHECK(fixture.model.target_of(standing, brake) == fms::kNoState);
  CHECK(fixture.model.target_of(driving, brake) == standing);
}

TEST_CASE("channels route back to their trigger") {
  Loaded fixture;
  REQUIRE(fixture.load(kMinimal) == fms::Status::Ok);

  CHECK(fixture.model.find_trigger_for_channel(sv("car/brakes/pressed")) ==
        fixture.model.find_trigger(sv("brake")));
  CHECK(fixture.model.find_trigger_for_channel(sv("car/nothing")) == fms::kNoTrigger);
}

TEST_CASE("a trigger without a channel listens on its own name") {
  Loaded fixture;
  REQUIRE(fixture.load(R"(
triggers:
  - {name: go}
  - {name: stop, channel: "bus/stop"}
states:
  - name: a
    transitions: {go: b, stop: a}
  - name: b
)") == fms::Status::Ok);

  CHECK(fixture.model.find_trigger_for_channel(sv("go")) ==
        fixture.model.find_trigger(sv("go")));
  CHECK(fixture.model.find_trigger_for_channel(sv("bus/stop")) ==
        fixture.model.find_trigger(sv("stop")));
  CHECK(fixture.model.find_trigger_for_channel(sv("stop")) == fms::kNoTrigger);
}

TEST_CASE("two triggers may not share a channel") {
  Loaded fixture;
  CHECK(fixture.load(R"(
triggers:
  - {name: one, channel: "bus/x"}
  - {name: two, channel: "bus/x"}
states: [{name: a}]
)") == fms::Status::DuplicateName);
}

TEST_CASE("malformed YAML comes back as ParseError, not as an exception") {
  Loaded fixture;
  CHECK(fixture.load("triggers: [unclosed\n  bad: : :") == fms::Status::ParseError);
  CHECK_FALSE(fixture.diagnostics.message.empty());
  CHECK(fixture.model.state_count() == 0);
}

TEST_CASE("dangling references are caught at load time") {
  Loaded fixture;

  SUBCASE("unknown target state") {
    CHECK(fixture.load(R"(
triggers: [{name: t}]
states:
  - name: a
    transitions: {t: nowhere}
)") == fms::Status::UnknownState);
  }

  SUBCASE("unknown trigger") {
    CHECK(fixture.load(R"(
triggers: [{name: t}]
states:
  - name: a
    transitions: {other: a}
)") == fms::Status::UnknownTrigger);
  }

  SUBCASE("duplicate state") {
    CHECK(fixture.load(R"(
triggers: [{name: t}]
states:
  - name: a
  - name: a
)") == fms::Status::DuplicateName);
  }

  // Note: a state cannot list the same trigger twice - 'transitions' is a YAML
  // mapping, so the key is unique by construction.

  CHECK_FALSE(fixture.diagnostics.message.empty());
  CHECK(fixture.diagnostics.line > 0);
  CHECK(fixture.model.state_count() == 0);  // a failed load leaves nothing behind
}

TEST_CASE("schema violations are reported") {
  Loaded fixture;

  SUBCASE("no states") {
    CHECK(fixture.load("triggers: [{name: t}]\n") == fms::Status::SchemaError);
  }
  SUBCASE("no triggers") {
    CHECK(fixture.load("states: [{name: a}]") == fms::Status::SchemaError);
  }
  SUBCASE("a state without a name") {
    CHECK(fixture.load("triggers: [{name: t}]\nstates: [{transitions: {t: a}}]") ==
          fms::Status::SchemaError);
  }
  SUBCASE("transitions is a sequence instead of a mapping") {
    CHECK(fixture.load(R"(
triggers: [{name: t}]
states:
  - name: a
    transitions: [t, a]
)") == fms::Status::SchemaError);
  }
  SUBCASE("the root is not a mapping") {
    CHECK(fixture.load("- just\n- a\n- list\n") == fms::Status::SchemaError);
  }
}

TEST_CASE("oversized names are rejected rather than silently truncated") {
  Loaded fixture;
  char   yaml[512];
  char   long_name[fms::limits::kMaxNameLength + 8];
  std::memset(long_name, 'x', sizeof(long_name));
  long_name[sizeof(long_name) - 1] = '\0';

  (void)std::snprintf(yaml, sizeof(yaml), "triggers: [{name: t}]\nstates: [{name: %s}]\n",
                      long_name);
  CHECK(fixture.load(yaml) == fms::Status::NameTooLong);
}
