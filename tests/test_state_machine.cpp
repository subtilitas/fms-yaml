// SPDX-License-Identifier: MIT
//
// The whole contract: a trigger the current state lists is a state change,
// anything else is Status::NoTransition and the state is untouched.
#include <doctest/doctest.h>

#include <cstring>

#include "fms/state_machine.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

/// standing --throttle--> accelerating --brake--> braking --stopped--> standing
struct Fixture {
  fms::Model        model;
  fms::Setup        setup;
  fms::StateMachine machine;

  fms::StateId standing = fms::kNoState;
  fms::StateId accelerating = fms::kNoState;
  fms::StateId braking = fms::kNoState;

  fms::TriggerId throttle = fms::kNoTrigger;
  fms::TriggerId brake    = fms::kNoTrigger;
  fms::TriggerId stopped  = fms::kNoTrigger;

  Fixture() {
    REQUIRE(model.declare_state(sv("standing"), standing) == fms::Status::Ok);
    REQUIRE(model.declare_state(sv("accelerating"), accelerating) == fms::Status::Ok);
    REQUIRE(model.declare_state(sv("braking"), braking) == fms::Status::Ok);

    REQUIRE(model.declare_trigger(sv("throttle"), sv("car/engine/throttle"), throttle) ==
            fms::Status::Ok);
    REQUIRE(model.declare_trigger(sv("brake"), sv("car/brakes/pressed"), brake) ==
            fms::Status::Ok);
    REQUIRE(model.declare_trigger(sv("stopped"), sv("car/wheels/stopped"), stopped) ==
            fms::Status::Ok);

    REQUIRE(model.add_transition(standing, throttle, accelerating) == fms::Status::Ok);
    REQUIRE(model.add_transition(accelerating, brake, braking) == fms::Status::Ok);
    REQUIRE(model.add_transition(braking, stopped, standing) == fms::Status::Ok);

    REQUIRE(model.validate() == fms::Status::Ok);
    REQUIRE(setup.set_initial(sv("standing")) == fms::Status::Ok);
    REQUIRE(machine.init(model, setup) == fms::Status::Ok);
  }
};

}  // namespace

TEST_CASE("start enters the initial state") {
  Fixture f;
  REQUIRE(f.machine.start() == fms::Status::Ok);
  CHECK(f.machine.current() == f.standing);
  CHECK(std::strcmp(f.machine.current_name(), "standing") == 0);
}

TEST_CASE("a matching trigger changes the state") {
  Fixture f;
  REQUIRE(f.machine.start() == fms::Status::Ok);

  fms::TransitionEvent event;
  REQUIRE(f.machine.fire(f.throttle, event) == fms::Status::Ok);

  CHECK(event.accepted);
  CHECK(event.from == f.standing);
  CHECK(event.to == f.accelerating);
  CHECK(event.trigger == f.throttle);
  CHECK(f.machine.current() == f.accelerating);
  CHECK(f.machine.transition_count() == 1);
}

TEST_CASE("a trigger the state does not list is an error and changes nothing") {
  Fixture f;
  REQUIRE(f.machine.start() == fms::Status::Ok);

  fms::TransitionEvent event;
  CHECK(f.machine.fire(f.stopped, event) == fms::Status::NoTransition);

  CHECK_FALSE(event.accepted);
  CHECK(event.from == f.standing);
  CHECK(event.to == fms::kNoState);
  CHECK(event.trigger == f.stopped);
  CHECK(f.machine.current() == f.standing);
  CHECK(f.machine.transition_count() == 0);
  CHECK(f.machine.rejection_count() == 1);
}

TEST_CASE("a full lap through the machine") {
  Fixture f;
  REQUIRE(f.machine.start() == fms::Status::Ok);

  CHECK(f.machine.fire(f.throttle) == fms::Status::Ok);
  CHECK(f.machine.fire(f.brake) == fms::Status::Ok);
  CHECK(f.machine.fire(f.stopped) == fms::Status::Ok);

  CHECK(f.machine.current() == f.standing);
  CHECK(f.machine.transition_count() == 3);
  CHECK(f.machine.rejection_count() == 0);
}

TEST_CASE("an unknown trigger id is rejected like any other") {
  Fixture f;
  REQUIRE(f.machine.start() == fms::Status::Ok);
  CHECK(f.machine.fire(999) == fms::Status::NoTransition);
  CHECK(f.machine.current() == f.standing);
}

TEST_CASE("firing before start is refused") {
  Fixture f;
  CHECK(f.machine.fire(f.throttle) == fms::Status::NotInitialised);
}

TEST_CASE("the model rejects duplicates and dangling references") {
  fms::Model model;

  fms::StateId a = fms::kNoState;
  fms::StateId duplicate = fms::kNoState;
  REQUIRE(model.declare_state(sv("a"), a) == fms::Status::Ok);
  CHECK(model.declare_state(sv("a"), duplicate) == fms::Status::DuplicateName);

  fms::TriggerId t = fms::kNoTrigger;
  fms::TriggerId t2 = fms::kNoTrigger;
  REQUIRE(model.declare_trigger(sv("t"), sv("x/y"), t) == fms::Status::Ok);
  CHECK(model.declare_trigger(sv("t"), sv("other"), t2) == fms::Status::DuplicateName);
  CHECK(model.declare_trigger(sv("other"), sv("x/y"), t2) == fms::Status::DuplicateName);

  CHECK(model.add_transition(a, t, 42) == fms::Status::UnknownState);
  CHECK(model.add_transition(42, t, a) == fms::Status::UnknownState);
  CHECK(model.add_transition(a, 42, a) == fms::Status::UnknownTrigger);

  // Adding the same trigger again appends another alternative - that is how
  // guarded branching is expressed - until the ceiling is reached.
  for (std::size_t i = 0; i < fms::limits::kMaxAlternatives; ++i) {
    REQUIRE(model.add_transition(a, t, a) == fms::Status::Ok);
  }
  CHECK(model.add_transition(a, t, a) == fms::Status::CapacityExceeded);

  // The model validates on its own: the initial state is the setup's business.
  CHECK(model.validate() == fms::Status::Ok);
}

TEST_CASE("an empty model does not validate") {
  fms::Model model;
  CHECK(model.validate() == fms::Status::SchemaError);
}

TEST_CASE("names longer than the compile-time limit are rejected, never truncated") {
  fms::Model model;

  char long_name[fms::limits::kMaxNameLength + 8];
  std::memset(long_name, 'x', sizeof(long_name));
  long_name[sizeof(long_name) - 1] = '\0';

  fms::StateId   state = fms::kNoState;
  fms::TriggerId trigger = fms::kNoTrigger;
  CHECK(model.declare_state(sv(long_name), state) == fms::Status::NameTooLong);
  CHECK(model.declare_trigger(sv(long_name), sv("x"), trigger) == fms::Status::NameTooLong);
}

TEST_CASE("capacity is a compile-time ceiling, not a suggestion") {
  fms::Model model;
  char       name[16];

  for (std::size_t i = 0; i < fms::limits::kMaxStates; ++i) {
    (void)std::snprintf(name, sizeof(name), "s%zu", i);
    fms::StateId id = fms::kNoState;
    REQUIRE(model.declare_state(sv(name), id) == fms::Status::Ok);
  }
  fms::StateId overflow = fms::kNoState;
  CHECK(model.declare_state(sv("one_too_many"), overflow) == fms::Status::CapacityExceeded);
}
