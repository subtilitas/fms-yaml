// SPDX-License-Identifier: MIT
//
// Channel -> trigger -> state change -> publication, and the error feedback for
// a trigger the current state does not accept.  Uses MemoryPort, so no I/O is
// involved.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/port/memory_port.hpp"
#include "fms/runtime.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

int g_traced   = 0;
int g_rejected = 0;

void trace(void*, const fms::TransitionEvent& event) {
  ++g_traced;
  if (!event.accepted) {
    ++g_rejected;
  }
}

constexpr const char* kSetup = R"(
fsm:
  name: car-under-test
  initial: standing
io:
  state_channel: "car/state"
  error_channel: "car/error"
  endpoint: "loopback"
  identity: "unit-test"
)";

constexpr const char* kMachine = R"(
fsm:
  name: car
triggers:
  - {name: throttle_pressed}
  - {name: brake_pressed, channel: "car/brakes/pressed"}
  - {name: vehicle_stopped}
states:
  - name: standing
    transitions:
      throttle_pressed: accelerating
  - name: accelerating
    transitions:
      brake_pressed: braking
  - name: braking
    transitions:
      vehicle_stopped: standing
)";

struct Harness {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::Runtime             runtime;
  fms::port::MemoryPort<>  port;
  fms::config::Diagnostics diagnostics;

  Harness() {
    g_traced   = 0;
    g_rejected = 0;

    REQUIRE(fms::config::load_setup_string(kSetup, setup, diagnostics) == fms::Status::Ok);
    const fms::Status loaded = fms::config::load_machine_string(kMachine, model, diagnostics);
    INFO("diagnostic: ", diagnostics.message.c_str());
    REQUIRE(loaded == fms::Status::Ok);

    REQUIRE(machine.init(model, setup) == fms::Status::Ok);
    REQUIRE(runtime.init(machine, port) == fms::Status::Ok);
    runtime.set_trace(&trace, nullptr);
    REQUIRE(runtime.start() == fms::Status::Ok);
  }

  void deliver(const char* channel) {
    REQUIRE(port.inject(sv(channel)) == fms::Status::Ok);
    REQUIRE(runtime.service(0) == fms::Status::Ok);
  }
};

}  // namespace

TEST_CASE("start configures and opens the port, announces every channel and publishes the initial state") {
  Harness h;
  CHECK(h.port.is_open());
  CHECK(h.port.listening().size() == 3);
  CHECK(h.port.last_state() == sv("standing"));
}

TEST_CASE("the io block of the setup reaches the port") {
  Harness h;
  // configure() ran before open(), with the values from the setup file.
  CHECK(h.port.configured());
  CHECK(sv(h.port.io().endpoint.c_str()) == sv("loopback"));
  CHECK(sv(h.port.io().identity.c_str()) == sv("unit-test"));
  CHECK(sv(h.port.io().state_channel.c_str()) == sv("car/state"));
  CHECK(h.port.configured_before_open());
}

TEST_CASE("a runtime cannot be bound to an uninitialised machine") {
  Harness            h;
  fms::StateMachine  fresh;
  fms::Runtime       runtime;
  fms::port::MemoryPort<> port;
  CHECK(runtime.init(fresh, port) == fms::Status::NotInitialised);
}

TEST_CASE("a trigger without an explicit channel listens on its own name") {
  Harness h;
  CHECK(h.model.find_trigger_for_channel(sv("throttle_pressed")) ==
        h.model.find_trigger(sv("throttle_pressed")));
  // ...and one with an explicit channel listens only there.
  CHECK(h.model.find_trigger_for_channel(sv("car/brakes/pressed")) ==
        h.model.find_trigger(sv("brake_pressed")));
  CHECK(h.model.find_trigger_for_channel(sv("brake_pressed")) == fms::kNoTrigger);
}

TEST_CASE("input on a trigger channel changes the state and publishes it") {
  Harness h;

  h.deliver("throttle_pressed");
  CHECK(std::strcmp(h.machine.current_name(), "accelerating") == 0);
  CHECK(h.port.last_state() == sv("accelerating"));

  h.deliver("car/brakes/pressed");
  CHECK(std::strcmp(h.machine.current_name(), "braking") == 0);
  CHECK(h.port.last_state() == sv("braking"));

  h.deliver("vehicle_stopped");
  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
  CHECK(h.machine.transition_count() == 3);
  CHECK(h.port.errors().empty());
}

TEST_CASE("a trigger the current state does not accept is reported as an error") {
  Harness h;
  h.port.clear_history();

  h.deliver("car/brakes/pressed");  // the car is standing

  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
  CHECK(h.machine.rejection_count() == 1);
  CHECK(h.port.last_error_message() == sv("rejected: brake_pressed in state standing"));
  CHECK(h.port.states().empty());  // the state did not change, so nothing was published
}

TEST_CASE("input on an unknown channel is counted and reported") {
  Harness h;
  h.deliver("radio_volume");

  CHECK(h.runtime.inputs_received() == 1);
  CHECK(h.runtime.inputs_unrouted() == 1);
  CHECK(h.machine.rejection_count() == 0);
  CHECK(h.port.last_error_message() == sv("unknown channel: radio_volume"));
  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
}

TEST_CASE("an idle port is not an error") {
  Harness h;
  CHECK(h.runtime.service(0) == fms::Status::Ok);  // nothing queued -> Timeout inside
  CHECK(h.runtime.inputs_received() == 0);
}

TEST_CASE("an exhausted port ends the loop") {
  Harness h;
  h.port.set_end_of_input();
  CHECK(h.runtime.service(0) == fms::Status::EndOfInput);
}

TEST_CASE("the trace hook sees accepted and rejected triggers alike") {
  Harness h;
  h.deliver("throttle_pressed");  // accepted
  h.deliver("vehicle_stopped");   // rejected while accelerating

  CHECK(g_traced == 2);
  CHECK(g_rejected == 1);
}

TEST_CASE("application code can raise a trigger by name") {
  Harness h;
  CHECK(h.runtime.fire_by_name(sv("throttle_pressed")) == fms::Status::Ok);
  CHECK(std::strcmp(h.machine.current_name(), "accelerating") == 0);

  CHECK(h.runtime.fire_by_name(sv("vehicle_stopped")) == fms::Status::NoTransition);
  CHECK(h.runtime.fire_by_name(sv("horn")) == fms::Status::UnknownTrigger);
}

TEST_CASE("stop closes the port") {
  Harness h;
  REQUIRE(h.runtime.stop() == fms::Status::Ok);
  CHECK_FALSE(h.port.is_open());
}
