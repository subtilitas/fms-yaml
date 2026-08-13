// SPDX-License-Identifier: MIT
//
// The load-bearing test for "no dynamic memory after setup".
//
// fms_alloc_guard replaces global operator new/delete.  Here the guard is armed
// in counting mode around a run phase that exercises every hot path: routing,
// state changes, rejections and publishing.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/alloc_guard.hpp"
#include "fms/mqtt/loopback_transport.hpp"
#include "fms/runtime.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

constexpr const char* kConfig = R"(
fsm:
  initial: standing
mqtt:
  qos: 0
  state_topic: "car/state"
  error_topic: "car/error"
triggers:
  - {name: throttle, topic: "car/engine/throttle"}
  - {name: brake,    topic: "car/brakes/pressed"}
  - {name: stopped,  topic: "car/wheels/stopped"}
states:
  - name: standing
    transitions: {throttle: accelerating}
  - name: accelerating
    transitions: {brake: braking}
  - name: braking
    transitions: {stopped: standing}
)";

// Statically allocated, exactly as an embedded target would have it.
fms::Model        g_model;
fms::StateMachine g_machine;
fms::Runtime      g_runtime;
fms::mqtt::LoopbackTransport<> g_transport;

}  // namespace

TEST_CASE("the run phase does not touch the heap") {
  // ---- setup phase: allocation is expected and allowed --------------------
  fms::config::Diagnostics diagnostics;
  const fms::Status loaded = fms::config::load_string(kConfig, g_model, diagnostics);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(loaded == fms::Status::Ok);

  REQUIRE(g_machine.init(g_model) == fms::Status::Ok);
  REQUIRE(g_runtime.init(g_model, g_machine, g_transport) == fms::Status::Ok);
  REQUIRE(g_runtime.start() == fms::Status::Ok);

  // ---- run phase ----------------------------------------------------------
  fms::alloc_guard::reset_counters();
  fms::alloc_guard::arm(/*fatal=*/false);

  for (int i = 0; i < 250; ++i) {
    g_transport.inject(sv("car/engine/throttle"));  // accepted
    g_runtime.service(0);

    g_transport.inject(sv("car/wheels/stopped"));   // rejected -> error publish
    g_runtime.service(0);

    g_transport.inject(sv("car/brakes/pressed"));   // accepted
    g_runtime.service(0);

    g_transport.inject(sv("car/wheels/stopped"));   // accepted, back to standing
    g_runtime.service(0);

    g_transport.inject(sv("car/radio/volume"));     // unknown topic
    g_runtime.service(0);
  }

  const std::size_t violations = fms::alloc_guard::violations();
  fms::alloc_guard::disarm();

  CHECK(violations == 0);
  CHECK(g_machine.transition_count() == 750);
  CHECK(g_machine.rejection_count() == 250);
  CHECK(g_runtime.messages_received() == 1250);
  CHECK(g_runtime.messages_unrouted() == 250);
  CHECK(std::strcmp(g_machine.current_name(), "standing") == 0);
}

TEST_CASE("the guard itself detects a heap allocation") {
  // Sanity check: without this, the test above would be worthless.
  fms::alloc_guard::reset_counters();
  fms::alloc_guard::arm(/*fatal=*/false);
  void* p = ::operator new(64);
  fms::alloc_guard::disarm();
  ::operator delete(p);

  CHECK(fms::alloc_guard::violations() == 1);
}
