// SPDX-License-Identifier: MIT
//
// Topic -> trigger -> state change -> publication, and the error feedback for
// a trigger the current state does not accept.  Uses LoopbackTransport, so no
// broker is involved.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/mqtt/loopback_transport.hpp"
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

constexpr const char* kConfig = R"(
fsm:
  name: car
  initial: standing
mqtt:
  client_id: "loopback"
  qos: 0
  state_topic: "car/state"
  error_topic: "car/error"
  retain_state: true
triggers:
  - {name: throttle_pressed, topic: "car/engine/throttle_pressed"}
  - {name: brake_pressed,    topic: "car/brakes/pressed"}
  - {name: vehicle_stopped,  topic: "car/wheels/stopped"}
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
  fms::Model               model;
  fms::StateMachine        machine;
  fms::Runtime             runtime;
  fms::mqtt::LoopbackTransport<> transport;
  fms::config::Diagnostics diagnostics;

  Harness() {
    g_traced   = 0;
    g_rejected = 0;

    const fms::Status loaded = fms::config::load_string(kConfig, model, diagnostics);
    INFO("diagnostic: ", diagnostics.message.c_str());
    REQUIRE(loaded == fms::Status::Ok);

    REQUIRE(machine.init(model) == fms::Status::Ok);
    REQUIRE(runtime.init(model, machine, transport) == fms::Status::Ok);
    runtime.set_trace(&trace, nullptr);
    REQUIRE(runtime.start() == fms::Status::Ok);
  }

  void deliver(const char* topic) {
    REQUIRE(transport.inject(sv(topic)) == fms::Status::Ok);
    REQUIRE(runtime.service(0) == fms::Status::Ok);
  }
};

}  // namespace

TEST_CASE("start subscribes to every trigger topic and publishes the initial state") {
  Harness h;
  CHECK(h.transport.subscriptions().size() == 3);
  CHECK(h.transport.connected());
  CHECK(h.transport.last_published_on(sv("car/state")) == sv("standing"));
}

TEST_CASE("a message on a trigger topic changes the state and publishes it") {
  Harness h;

  h.deliver("car/engine/throttle_pressed");
  CHECK(std::strcmp(h.machine.current_name(), "accelerating") == 0);
  CHECK(h.transport.last_published_on(sv("car/state")) == sv("accelerating"));

  h.deliver("car/brakes/pressed");
  CHECK(std::strcmp(h.machine.current_name(), "braking") == 0);
  CHECK(h.transport.last_published_on(sv("car/state")) == sv("braking"));

  h.deliver("car/wheels/stopped");
  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
  CHECK(h.machine.transition_count() == 3);
}

TEST_CASE("a trigger the current state does not accept is reported on the error topic") {
  Harness h;
  h.transport.clear_published();

  h.deliver("car/brakes/pressed");  // the car is standing

  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
  CHECK(h.machine.rejection_count() == 1);
  CHECK(h.transport.last_published_on(sv("car/error")) ==
        sv("rejected: brake_pressed in state standing"));
  // Nothing was published on the state topic: the state did not change.
  CHECK(h.transport.last_published_on(sv("car/state")).empty());
}

TEST_CASE("a message on an unknown topic is counted and ignored") {
  Harness h;
  h.deliver("car/radio/volume");

  CHECK(h.runtime.messages_received() == 1);
  CHECK(h.runtime.messages_unrouted() == 1);
  CHECK(h.machine.rejection_count() == 0);
  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
}

TEST_CASE("the payload is not part of the decision") {
  Harness h;
  REQUIRE(h.transport.inject(sv("car/engine/throttle_pressed"), sv("anything at all")) ==
          fms::Status::Ok);
  REQUIRE(h.runtime.service(0) == fms::Status::Ok);
  CHECK(std::strcmp(h.machine.current_name(), "accelerating") == 0);
}

TEST_CASE("the trace hook sees accepted and rejected triggers alike") {
  Harness h;
  h.deliver("car/engine/throttle_pressed");  // accepted
  h.deliver("car/wheels/stopped");           // rejected while accelerating

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

TEST_CASE("stop disconnects") {
  Harness h;
  REQUIRE(h.runtime.stop() == fms::Status::Ok);
  CHECK_FALSE(h.transport.connected());
}
