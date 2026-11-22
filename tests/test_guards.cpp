// SPDX-License-Identifier: MIT
//
// Guards end to end: the three ways of spelling a transition, the order
// alternatives are tried in, and what the machine and the runtime report when
// nothing holds.
#include <doctest/doctest.h>

#include "fms/port/memory_port.hpp"
#include "fms/runtime.hpp"
#include "fms/limits.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text); }

constexpr const char* kSetup = "fsm: {initial: self_test}\n";

constexpr const char* kMachine = R"(
triggers:
  - {name: self_test_passed}
  - {name: throttle_pressed}
  - {name: engine_fault}
  - {name: ignition_off}
states:
  - name: self_test
    transitions:
      # ordered alternatives: first match wins, the last one is the fallback
      self_test_passed:
        - {when: "errors == 0", target: standing}
        - {target: fault}
      ignition_off: power_off
  - name: standing
    transitions:
      # a single guarded alternative and no fallback: a light touch is rejected
      throttle_pressed: {when: "pedal > 5", target: accelerating}
      # two conditions, ANDed
      engine_fault:
        - {when: ["severity >= 2", "system == engine"], target: fault}
        - {target: standing}
  - name: accelerating
  - name: fault
  - name: power_off
)";

struct Harness {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::Runtime             runtime;
  fms::port::MemoryPort<>  port;
  fms::config::Diagnostics diagnostics;

  Harness() {
    REQUIRE(fms::config::load_setup_string(kSetup, setup, diagnostics) == fms::Status::Ok);
    const fms::Status loaded = fms::config::load_machine_string(kMachine, model, diagnostics);
    INFO("diagnostic: ", diagnostics.message.c_str());
    REQUIRE(loaded == fms::Status::Ok);
    REQUIRE(machine.init(model, setup) == fms::Status::Ok);
    REQUIRE(runtime.init(machine, port) == fms::Status::Ok);
    REQUIRE(runtime.start() == fms::Status::Ok);
  }

  void deliver(const char* channel, const char* arguments = "") {
    REQUIRE(port.inject(sv(channel), sv(arguments)) == fms::Status::Ok);
    REQUIRE(runtime.service(0) == fms::Status::Ok);
  }
};

}  // namespace

/// A message equal to `expected`, or to as much of it as this build's
/// FMS_MAX_MESSAGE_LENGTH can hold.  Comparing against the whole sentence made
/// this an assertion about the default message capacity rather than about what
/// the runtime reports.
bool reports(fms::StringView actual, const char* expected) {
  const fms::StringView full = sv(expected);
  const std::size_t      want = (full.size() < fms::limits::kMaxMessageLength)
                                    ? full.size()
                                    : fms::limits::kMaxMessageLength;
  return actual == full.substr(0, want);
}

TEST_CASE("the first alternative whose guard holds is taken") {
  Harness h;
  h.deliver("self_test_passed", "errors=0");
  CHECK(sv(h.machine.current_name()) == sv("standing"));
}

TEST_CASE("an unguarded alternative is the fallback") {
  Harness h;
  h.deliver("self_test_passed", "errors=3");
  CHECK(sv(h.machine.current_name()) == sv("fault"));
}

TEST_CASE("a guard that needs an argument nobody sent takes the fallback") {
  Harness h;
  h.deliver("self_test_passed");  // no errors= at all
  CHECK(sv(h.machine.current_name()) == sv("fault"));
}

TEST_CASE("with no fallback, a failing guard is a rejection") {
  Harness h;
  h.deliver("self_test_passed", "errors=0");
  REQUIRE(sv(h.machine.current_name()) == sv("standing"));
  h.port.clear_history();

  h.deliver("throttle_pressed", "pedal=3");

  CHECK(sv(h.machine.current_name()) == sv("standing"));
  CHECK(h.machine.rejection_count() == 1);
  CHECK(h.port.states().empty());
  // The report says which guard failed and with what, which is the useful part.
  CHECK(reports(h.port.last_error_message(),
                "rejected: throttle_pressed in state standing: no guard matched (pedal=3)"));

  h.deliver("throttle_pressed", "pedal=40");
  CHECK(sv(h.machine.current_name()) == sv("accelerating"));
}

TEST_CASE("a guard rejection is told apart from an unknown trigger") {
  Harness h;
  h.deliver("self_test_passed", "errors=0");  // -> standing

  fms::Args args;
  REQUIRE(args.parse(sv("pedal=1")) == fms::Status::Ok);
  fms::TransitionEvent event;

  const fms::TriggerId throttle = h.model.find_trigger(sv("throttle_pressed"));
  CHECK(h.machine.fire(throttle, args, event) == fms::Status::GuardRejected);
  CHECK(event.guard_rejected);
  CHECK_FALSE(event.accepted);

  const fms::TriggerId ignition_off = h.model.find_trigger(sv("ignition_off"));
  CHECK(h.machine.fire(ignition_off, event) == fms::Status::NoTransition);
  CHECK_FALSE(event.guard_rejected);  // standing does not list it at all
}

TEST_CASE("conditions listed together are ANDed") {
  Harness h;
  h.deliver("self_test_passed", "errors=0");  // -> standing

  h.deliver("engine_fault", "severity=3 system=gearbox");  // second condition fails
  CHECK(sv(h.machine.current_name()) == sv("standing"));

  h.deliver("engine_fault", "severity=1 system=engine");   // first condition fails
  CHECK(sv(h.machine.current_name()) == sv("standing"));

  h.deliver("engine_fault", "severity=2 system=engine");   // both hold
  CHECK(sv(h.machine.current_name()) == sv("fault"));
}

TEST_CASE("the model answers what a state accepts and why") {
  Harness h;
  const fms::StateId   standing = h.model.find_state(sv("standing"));
  const fms::TriggerId throttle = h.model.find_trigger(sv("throttle_pressed"));
  const fms::TriggerId ignition = h.model.find_trigger(sv("ignition_off"));

  CHECK(h.model.accepts(standing, throttle));
  CHECK_FALSE(h.model.accepts(standing, ignition));

  fms::Args args;
  REQUIRE(args.parse(sv("pedal=90")) == fms::Status::Ok);
  fms::StateId target = fms::kNoState;
  CHECK(h.model.evaluate(standing, throttle, args, target) == fms::Decision::Accepted);
  CHECK(target == h.model.find_state(sv("accelerating")));

  REQUIRE(args.parse(sv("pedal=1")) == fms::Status::Ok);
  CHECK(h.model.evaluate(standing, throttle, args, target) == fms::Decision::GuardRejected);
  CHECK(target == fms::kNoState);

  CHECK(h.model.evaluate(standing, ignition, args, target) == fms::Decision::NoTransition);
}

TEST_CASE("malformed arguments are reported and change nothing") {
  Harness h;
  h.deliver("self_test_passed", "errors");  // no '='

  CHECK(sv(h.machine.current_name()) == sv("self_test"));
  CHECK(h.machine.transition_count() == 0);
  CHECK(h.port.last_error_message() ==
        sv("bad arguments for self_test_passed: malformed trigger arguments"));
}

TEST_CASE("application code can pass arguments too") {
  Harness h;
  CHECK(h.runtime.fire_by_name(sv("self_test_passed"), sv("errors=0")) == fms::Status::Ok);
  CHECK(sv(h.machine.current_name()) == sv("standing"));

  CHECK(h.runtime.fire_by_name(sv("throttle_pressed"), sv("pedal=2")) ==
        fms::Status::GuardRejected);
  CHECK(h.runtime.fire_by_name(sv("throttle_pressed"), sv("nonsense")) ==
        fms::Status::ArgumentError);
}

TEST_CASE("guards that do not parse are a config error with a line number") {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  CHECK(fms::config::load_machine_string(R"(
triggers: [{name: t}]
states:
  - name: a
    transitions:
      t: {when: "pedal", target: a}
)",
                                        model, diagnostics) == fms::Status::SchemaError);
  CHECK(diagnostics.line == 6);
  CHECK(diagnostics.message.find("guard") != fms::Message::npos);

  CHECK(fms::config::load_machine_string(R"(
triggers: [{name: t}]
states:
  - name: a
    transitions:
      t: {when: "x > 1"}
)",
                                        model, diagnostics) == fms::Status::SchemaError);
  CHECK(diagnostics.message.find("target") != fms::Message::npos);
}

TEST_CASE("all three spellings of a transition load") {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  const fms::Status status = fms::config::load_machine_string(R"(
triggers: [{name: t}]
states:
  - name: plain
    transitions: {t: guarded}
  - name: guarded
    transitions:
      t: {when: "x > 1", target: listed}
  - name: listed
    transitions:
      t:
        - {when: "x > 10", target: plain}
        - {target: guarded}
)",
                                                             model, diagnostics);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(status == fms::Status::Ok);
  CHECK(model.condition_count() == 2);  // one per guarded alternative
}

TEST_CASE("the shipped car machine uses guards as documented") {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::Runtime             runtime;
  fms::port::MemoryPort<>  port;
  fms::config::Diagnostics diagnostics;

  REQUIRE(fms::config::load_setup_file(FMS_SOURCE_DIR "/examples/car/car.setup.yaml", setup,
                                       diagnostics) == fms::Status::Ok);
  const fms::Status loaded = fms::config::load_machine_file(
      FMS_SOURCE_DIR "/examples/car/car.machine.yaml", model, diagnostics);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(loaded == fms::Status::Ok);
  REQUIRE(machine.init(model, setup) == fms::Status::Ok);
  REQUIRE(runtime.init(machine, port) == fms::Status::Ok);
  REQUIRE(runtime.start() == fms::Status::Ok);

  const auto deliver = [&](const char* channel, const char* arguments) {
    REQUIRE(port.inject(sv(channel), sv(arguments)) == fms::Status::Ok);
    REQUIRE(runtime.service(0) == fms::Status::Ok);
  };

  deliver("ignition_on", "");
  CHECK(sv(machine.current_name()) == sv("self_test"));

  deliver("self_test_passed", "errors=2");   // a failed self test parks the car
  CHECK(sv(machine.current_name()) == sv("fault"));

  deliver("ignition_off", "");
  deliver("ignition_on", "");
  deliver("self_test_passed", "errors=0");   // a clean one releases it
  CHECK(sv(machine.current_name()) == sv("standing"));

  deliver("throttle_pressed", "pedal=3");    // resting a foot on the pedal
  CHECK(sv(machine.current_name()) == sv("standing"));

  deliver("throttle_pressed", "pedal=60");
  CHECK(sv(machine.current_name()) == sv("accelerating"));

  deliver("engine_fault", "severity=1");     // a warning does not stop the car
  CHECK(sv(machine.current_name()) == sv("accelerating"));

  deliver("engine_fault", "severity=3");
  CHECK(sv(machine.current_name()) == sv("fault"));
}
