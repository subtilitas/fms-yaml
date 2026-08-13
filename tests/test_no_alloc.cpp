// SPDX-License-Identifier: MIT
//
// The load-bearing test for "no dynamic memory after setup".
//
// fms_alloc_guard replaces global operator new/delete.  Here the guard is armed
// in counting mode around a run phase that exercises every hot path: routing,
// state changes, rejections, unknown channels and publishing.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/alloc_guard.hpp"
#include "fms/port/memory_port.hpp"
#include "fms/runtime.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

constexpr const char* kSetup = R"(
fsm:
  initial: standing
io:
  state_channel: "car/state"
  error_channel: "car/error"
)";

constexpr const char* kMachine = R"(
triggers:
  - {name: throttle}
  - {name: brake}
  - {name: stopped}
states:
  - name: standing
    transitions: {throttle: accelerating}
  - name: accelerating
    transitions: {brake: braking}
  - name: braking
    transitions: {stopped: standing}
)";

// Statically allocated, exactly as an embedded target would have it.
fms::Setup              g_setup;
fms::Model              g_model;
fms::StateMachine       g_machine;
fms::Runtime            g_runtime;
fms::port::MemoryPort<> g_port;

}  // namespace

TEST_CASE("the run phase does not touch the heap") {
  // ---- setup phase: allocation is expected and allowed --------------------
  fms::config::Diagnostics diagnostics;
  REQUIRE(fms::config::load_setup_string(kSetup, g_setup, diagnostics) == fms::Status::Ok);
  const fms::Status loaded = fms::config::load_machine_string(kMachine, g_model, diagnostics);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(loaded == fms::Status::Ok);

  REQUIRE(g_machine.init(g_model, g_setup) == fms::Status::Ok);
  REQUIRE(g_runtime.init(g_machine, g_port) == fms::Status::Ok);
  REQUIRE(g_runtime.start() == fms::Status::Ok);

  // ---- run phase ----------------------------------------------------------
  fms::alloc_guard::reset_counters();
  fms::alloc_guard::arm(/*fatal=*/false);

  for (int i = 0; i < 250; ++i) {
    g_port.inject(sv("throttle"));       // accepted
    g_runtime.service(0);

    g_port.inject(sv("stopped"));        // rejected -> error published
    g_runtime.service(0);

    g_port.inject(sv("brake"));          // accepted
    g_runtime.service(0);

    g_port.inject(sv("stopped"));        // accepted, back to standing
    g_runtime.service(0);

    g_port.inject(sv("radio_volume"));   // unknown channel -> error published
    g_runtime.service(0);

    g_runtime.service(0);                // idle poll
  }

  const std::size_t violations = fms::alloc_guard::violations();
  fms::alloc_guard::disarm();

  CHECK(violations == 0);
  CHECK(g_machine.transition_count() == 750);
  CHECK(g_machine.rejection_count() == 250);
  CHECK(g_runtime.inputs_received() == 1250);
  CHECK(g_runtime.inputs_unrouted() == 250);
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
