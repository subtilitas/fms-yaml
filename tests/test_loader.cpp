// SPDX-License-Identifier: MIT
//
// The loader is the exception firewall: every case here must come back as a
// Status with a useful diagnostic, never as a throw.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

struct Loaded {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  fms::Status load(const char* yaml) {
    return fms::config::load_string(yaml, model, diagnostics);
  }
};

constexpr const char* kMinimal = R"(
fsm:
  name: minimal
  initial: standing
mqtt:
  client_id: "test"
  qos: 1
  state_topic: "car/state"
  error_topic: "car/error"
triggers:
  - {name: throttle, topic: "car/engine/throttle"}
  - {name: brake,    topic: "car/brakes/pressed"}
states:
  - name: standing
    transitions:
      throttle: driving
  - name: driving
    transitions:
      brake: standing
)";

}  // namespace

TEST_CASE("a valid document produces a validated model") {
  Loaded fixture;
  INFO("diagnostic: ", fixture.diagnostics.message.c_str());
  REQUIRE(fixture.load(kMinimal) == fms::Status::Ok);

  CHECK(std::strcmp(fixture.model.name().c_str(), "minimal") == 0);
  CHECK(fixture.model.state_count() == 2);
  CHECK(fixture.model.trigger_count() == 2);
  CHECK(fixture.model.initial() == fixture.model.find_state(sv("standing")));
  CHECK(fixture.model.validate() == fms::Status::Ok);

  const fms::StateId standing = fixture.model.find_state(sv("standing"));
  const fms::StateId driving  = fixture.model.find_state(sv("driving"));
  const fms::TriggerId throttle = fixture.model.find_trigger(sv("throttle"));
  const fms::TriggerId brake    = fixture.model.find_trigger(sv("brake"));

  CHECK(fixture.model.target_of(standing, throttle) == driving);
  CHECK(fixture.model.target_of(standing, brake) == fms::kNoState);
  CHECK(fixture.model.target_of(driving, brake) == standing);

  CHECK(std::strcmp(fixture.model.mqtt().client_id.c_str(), "test") == 0);
  CHECK(fixture.model.mqtt().qos == 1);
}

TEST_CASE("topics route back to their trigger") {
  Loaded fixture;
  REQUIRE(fixture.load(kMinimal) == fms::Status::Ok);

  CHECK(fixture.model.find_trigger_for_topic(sv("car/brakes/pressed")) ==
        fixture.model.find_trigger(sv("brake")));
  CHECK(fixture.model.find_trigger_for_topic(sv("car/nothing")) == fms::kNoTrigger);
}

TEST_CASE("malformed YAML comes back as ParseError, not as an exception") {
  Loaded fixture;
  CHECK(fixture.load("fsm: [unclosed\n  bad: : :") == fms::Status::ParseError);
  CHECK_FALSE(fixture.diagnostics.message.empty());
  CHECK(fixture.model.state_count() == 0);
}

TEST_CASE("dangling references are caught at load time") {
  Loaded fixture;

  SUBCASE("unknown target state") {
    CHECK(fixture.load(R"(
fsm: {initial: a}
triggers: [{name: t, topic: "x"}]
states:
  - name: a
    transitions: {t: nowhere}
)") == fms::Status::UnknownState);
  }

  SUBCASE("unknown trigger") {
    CHECK(fixture.load(R"(
fsm: {initial: a}
triggers: [{name: t, topic: "x"}]
states:
  - name: a
    transitions: {other: a}
)") == fms::Status::UnknownTrigger);
  }

  SUBCASE("unknown initial state") {
    CHECK(fixture.load(R"(
fsm: {initial: ghost}
triggers: [{name: t, topic: "x"}]
states: [{name: a}]
)") == fms::Status::UnknownState);
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
    CHECK(fixture.load("fsm: {initial: a}\ntriggers: [{name: t, topic: x}]\n") ==
          fms::Status::SchemaError);
  }
  SUBCASE("no initial") {
    CHECK(fixture.load("fsm: {name: x}\ntriggers: [{name: t, topic: x}]\nstates: [{name: a}]") ==
          fms::Status::SchemaError);
  }
  SUBCASE("no triggers") {
    CHECK(fixture.load("fsm: {initial: a}\nstates: [{name: a}]") == fms::Status::SchemaError);
  }
  SUBCASE("transitions is a sequence instead of a mapping") {
    CHECK(fixture.load(R"(
fsm: {initial: a}
triggers: [{name: t, topic: "x"}]
states:
  - name: a
    transitions: [t, a]
)") == fms::Status::SchemaError);
  }
  SUBCASE("qos out of range") {
    CHECK(fixture.load(R"(
fsm: {initial: a}
mqtt: {qos: 7}
triggers: [{name: t, topic: "x"}]
states: [{name: a}]
)") == fms::Status::SchemaError);
  }
}

TEST_CASE("oversized names are rejected rather than silently truncated") {
  Loaded fixture;
  char yaml[512];
  char long_name[fms::limits::kMaxNameLength + 8];
  std::memset(long_name, 'x', sizeof(long_name));
  long_name[sizeof(long_name) - 1] = '\0';

  std::snprintf(yaml, sizeof(yaml),
                "fsm: {initial: %s}\ntriggers: [{name: t, topic: \"x\"}]\nstates: [{name: %s}]\n",
                long_name, long_name);
  CHECK(fixture.load(yaml) == fms::Status::NameTooLong);
}

TEST_CASE("a missing file is a status, not a crash") {
  Loaded fixture;
  CHECK(fms::config::load_file("/definitely/not/here.yaml", fixture.model,
                               fixture.diagnostics) == fms::Status::FileNotFound);
}

TEST_CASE("the shipped car config loads") {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  const fms::Status status =
      fms::config::load_file(FMS_SOURCE_DIR "/examples/car/car.yaml", model, diagnostics);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(status == fms::Status::Ok);

  CHECK(model.state_count() == 7);
  CHECK(model.trigger_count() == 10);
  CHECK(model.initial() == model.find_state(sv("power_off")));

  // The brakes are only relevant once the car is moving.
  const fms::TriggerId brake = model.find_trigger(sv("brake_pressed"));
  CHECK(model.target_of(model.find_state(sv("accelerating")), brake) ==
        model.find_state(sv("braking")));
  CHECK(model.target_of(model.find_state(sv("power_off")), brake) == fms::kNoState);
}
