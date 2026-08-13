// SPDX-License-Identifier: MIT
//
// The setup half of the configuration, and the one cross-file check: the
// initial state it names has to exist in the machine it is bound to.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/state_machine.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

constexpr const char* kSetup = R"(
fsm:
  name: bench-01
  initial: standing
io:
  state_channel: "car/state"
  error_channel: "car/error"
  endpoint: "tcp://localhost:1883"
  identity: "car-ecu-01"
)";

constexpr const char* kMachine = R"(
fsm:
  name: car
triggers:
  - {name: throttle}
  - {name: brake}
states:
  - name: standing
    transitions: {throttle: driving}
  - name: driving
    transitions: {brake: standing}
)";

struct Pair {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::config::Diagnostics diagnostics;

  Pair() {
    REQUIRE(fms::config::load_setup_string(kSetup, setup, diagnostics) == fms::Status::Ok);
    REQUIRE(fms::config::load_machine_string(kMachine, model, diagnostics) == fms::Status::Ok);
  }
};

}  // namespace

TEST_CASE("the setup file carries the deployment, not the behaviour") {
  Pair p;

  CHECK(std::strcmp(p.setup.name().c_str(), "bench-01") == 0);
  CHECK(std::strcmp(p.setup.initial_name().c_str(), "standing") == 0);
  CHECK(std::strcmp(p.setup.io().state_channel.c_str(), "car/state") == 0);
  CHECK(std::strcmp(p.setup.io().error_channel.c_str(), "car/error") == 0);
  CHECK(std::strcmp(p.setup.io().endpoint.c_str(), "tcp://localhost:1883") == 0);
  CHECK(std::strcmp(p.setup.io().identity.c_str(), "car-ecu-01") == 0);
}

TEST_CASE("the machine file carries the behaviour, not the deployment") {
  Pair p;

  CHECK(std::strcmp(p.model.name().c_str(), "car") == 0);
  CHECK(p.model.state_count() == 2);
  CHECK(p.model.trigger_count() == 2);
  CHECK(p.model.validate() == fms::Status::Ok);  // valid without any setup
}

TEST_CASE("binding resolves the initial state") {
  Pair p;
  REQUIRE(p.machine.init(p.model, p.setup) == fms::Status::Ok);

  CHECK(p.machine.initial() == p.model.find_state(sv("standing")));
  REQUIRE(p.machine.start() == fms::Status::Ok);
  CHECK(std::strcmp(p.machine.current_name(), "standing") == 0);
}

TEST_CASE("one machine can be started in different states by swapping setups") {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;
  REQUIRE(fms::config::load_machine_string(kMachine, model, diagnostics) == fms::Status::Ok);

  fms::Setup        bench;
  fms::StateMachine machine_a;
  REQUIRE(fms::config::load_setup_string("fsm: {name: bench, initial: driving}", bench,
                                         diagnostics) == fms::Status::Ok);
  REQUIRE(machine_a.init(model, bench) == fms::Status::Ok);
  REQUIRE(machine_a.start() == fms::Status::Ok);
  CHECK(std::strcmp(machine_a.current_name(), "driving") == 0);

  fms::Setup        car;
  fms::StateMachine machine_b;
  REQUIRE(fms::config::load_setup_string("fsm: {name: car, initial: standing}", car,
                                         diagnostics) == fms::Status::Ok);
  REQUIRE(machine_b.init(model, car) == fms::Status::Ok);
  REQUIRE(machine_b.start() == fms::Status::Ok);
  CHECK(std::strcmp(machine_b.current_name(), "standing") == 0);
}

TEST_CASE("a setup naming a state the machine does not have is caught at init") {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::config::Diagnostics diagnostics;

  REQUIRE(fms::config::load_setup_string("fsm: {initial: ghost}", setup, diagnostics) ==
          fms::Status::Ok);  // loading the setup alone cannot know better
  REQUIRE(fms::config::load_machine_string(kMachine, model, diagnostics) == fms::Status::Ok);

  CHECK(setup.validate_against(model) == fms::Status::UnknownState);
  CHECK(machine.init(model, setup) == fms::Status::UnknownState);
  CHECK(machine.start() == fms::Status::NotInitialised);
}

TEST_CASE("the setup file rejects machine sections and vice versa") {
  fms::Setup               setup;
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  SUBCASE("states in the setup file") {
    CHECK(fms::config::load_setup_string("fsm: {initial: a}\nstates: [{name: a}]", setup,
                                         diagnostics) == fms::Status::SchemaError);
    CHECK(diagnostics.message.find("machine") != fms::Message::npos);
  }
  SUBCASE("triggers in the setup file") {
    CHECK(fms::config::load_setup_string("fsm: {initial: a}\ntriggers: [{name: t}]", setup,
                                         diagnostics) == fms::Status::SchemaError);
  }
  SUBCASE("io in the machine file") {
    CHECK(fms::config::load_machine_string(
              "io: {endpoint: x}\ntriggers: [{name: t}]\nstates: [{name: a}]", model,
              diagnostics) == fms::Status::SchemaError);
    CHECK(diagnostics.message.find("setup") != fms::Message::npos);
  }
  SUBCASE("fsm.initial in the machine file") {
    CHECK(fms::config::load_machine_string(
              "fsm: {initial: a}\ntriggers: [{name: t}]\nstates: [{name: a}]", model,
              diagnostics) == fms::Status::SchemaError);
  }
}

TEST_CASE("a setup without an initial state is a schema error") {
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;
  CHECK(fms::config::load_setup_string("fsm: {name: x}", setup, diagnostics) ==
        fms::Status::SchemaError);
  CHECK(fms::config::load_setup_string("io: {endpoint: x}", setup, diagnostics) ==
        fms::Status::SchemaError);
}

TEST_CASE("a missing file is a status, not a crash") {
  fms::Setup               setup;
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  CHECK(fms::config::load_setup_file("/definitely/not/here.yaml", setup, diagnostics) ==
        fms::Status::FileNotFound);
  CHECK(fms::config::load_machine_file("/definitely/not/here.yaml", model, diagnostics) ==
        fms::Status::FileNotFound);
}

TEST_CASE("the shipped car configuration loads as a pair") {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::config::Diagnostics diagnostics;

  REQUIRE(fms::config::load_setup_file(FMS_SOURCE_DIR "/examples/car/car.setup.yaml", setup,
                                       diagnostics) == fms::Status::Ok);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(fms::config::load_machine_file(FMS_SOURCE_DIR "/examples/car/car.machine.yaml", model,
                                         diagnostics) == fms::Status::Ok);

  CHECK(model.state_count() == 7);
  CHECK(model.trigger_count() == 10);
  REQUIRE(machine.init(model, setup) == fms::Status::Ok);
  CHECK(machine.initial() == model.find_state(sv("power_off")));

  // The brakes are only relevant once the car is moving.
  const fms::TriggerId brake = model.find_trigger(sv("brake_pressed"));
  CHECK(model.target_of(model.find_state(sv("accelerating")), brake) ==
        model.find_state(sv("braking")));
  CHECK(model.target_of(model.find_state(sv("power_off")), brake) == fms::kNoState);
}
