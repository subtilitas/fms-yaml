// SPDX-License-Identifier: MIT
//
// The setup half of the configuration, and the one cross-file check: the
// initial state it names has to exist in the machine it is bound to.
#include <doctest/doctest.h>

#include <cstring>
#include <string>

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

TEST_CASE("a null argument is refused rather than dereferenced") {
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  SUBCASE("no buffer") {
    CHECK(fms::config::load_setup_string(nullptr, setup, diagnostics) ==
          fms::Status::InvalidArgument);
    CHECK(diagnostics.status == fms::Status::InvalidArgument);
  }
  SUBCASE("no path") {
    CHECK(fms::config::load_setup_file(nullptr, setup, diagnostics) ==
          fms::Status::InvalidArgument);
    CHECK(diagnostics.status == fms::Status::InvalidArgument);
  }
}

TEST_CASE("an io value longer than its field is refused, never truncated") {
  // Silently shortening an endpoint or a client id would connect the machine
  // somewhere other than the file says, which is worse than refusing to start.
  const std::string long_channel(fms::limits::kMaxChannelLength + 1, 'c');
  const std::string long_name(fms::limits::kMaxNameLength + 1, 'n');

  const auto with_io = [](const char* key, const std::string& value) {
    return std::string("fsm:\n  initial: standing\nio:\n  ") + key + ": \"" + value + "\"\n";
  };

  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  SUBCASE("state_channel") {
    const std::string yaml = with_io("state_channel", long_channel);
    CHECK(fms::config::load_setup_string(yaml.c_str(), setup, diagnostics) ==
          fms::Status::ChannelTooLong);
  }
  SUBCASE("error_channel") {
    const std::string yaml = with_io("error_channel", long_channel);
    CHECK(fms::config::load_setup_string(yaml.c_str(), setup, diagnostics) ==
          fms::Status::ChannelTooLong);
  }
  SUBCASE("endpoint") {
    const std::string yaml = with_io("endpoint", long_channel);
    CHECK(fms::config::load_setup_string(yaml.c_str(), setup, diagnostics) ==
          fms::Status::ChannelTooLong);
  }
  SUBCASE("identity") {
    const std::string yaml = with_io("identity", long_name);
    CHECK(fms::config::load_setup_string(yaml.c_str(), setup, diagnostics) ==
          fms::Status::NameTooLong);
  }

  CHECK(diagnostics.line > 0);
  CHECK(setup.io().state_channel.empty());
}

TEST_CASE("io must be a mapping, and says so with a line number") {
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  CHECK(fms::config::load_setup_string("fsm:\n  initial: standing\nio:\n  - one\n  - two\n",
                                       setup, diagnostics) == fms::Status::SchemaError);
  CHECK(diagnostics.line > 0);
}

TEST_CASE("io is optional, and its absence is not an error") {
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  REQUIRE(fms::config::load_setup_string("fsm:\n  initial: standing\n", setup, diagnostics) ==
          fms::Status::Ok);
  CHECK(setup.io().state_channel.empty());
  CHECK(setup.io().endpoint.empty());
}

TEST_CASE("a setup name longer than the limit is refused") {
  const std::string long_name(fms::limits::kMaxNameLength + 1, 'n');
  const std::string yaml = "fsm:\n  name: \"" + long_name + "\"\n  initial: standing\n";

  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;
  CHECK(fms::config::load_setup_string(yaml.c_str(), setup, diagnostics) ==
        fms::Status::NameTooLong);
}

TEST_CASE("a Setup insists on being told where to start") {
  fms::Setup setup;

  // Not merely absent from the file - refused outright, so a caller building a
  // Setup in code cannot end up with an empty initial state either.
  CHECK(setup.set_initial(fms::StringView{}) == fms::Status::InvalidArgument);
  CHECK(setup.initial_name().empty());

  fms::Model model;
  fms::StateId id = fms::kNoState;
  REQUIRE(model.declare_state(sv("standing"), id) == fms::Status::Ok);

  // Nothing to resolve, so binding fails before it can look anything up.
  CHECK(setup.validate_against(model) == fms::Status::SchemaError);
  CHECK(setup.initial_in(model) == fms::kNoState);

  REQUIRE(setup.set_initial(sv("standing")) == fms::Status::Ok);
  CHECK(setup.validate_against(model) == fms::Status::Ok);
  CHECK(setup.initial_in(model) == id);
}

TEST_CASE("clear returns a Setup to the state it was constructed in") {
  Pair pair;
  REQUIRE(!pair.setup.initial_name().empty());

  pair.setup.clear();
  CHECK(pair.setup.name().empty());
  CHECK(pair.setup.initial_name().empty());
  CHECK(pair.setup.io().state_channel.empty());
  CHECK(pair.setup.io().endpoint.empty());
}

TEST_CASE("the setup loader refuses an unreadable path the same way") {
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  const fms::Status status =
      fms::config::load_setup_file(FMS_SOURCE_DIR "/examples", setup, diagnostics);

  CHECK(status != fms::Status::Ok);
  CHECK(status != fms::Status::ParseError);
  CHECK((status == fms::Status::FileNotReadable || status == fms::Status::FileNotFound));
  CHECK(setup.initial_name().size() == 0);
}
