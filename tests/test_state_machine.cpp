// SPDX-License-Identifier: MIT
//
// The whole contract: a trigger the current state lists is a state change,
// anything else is Status::NoTransition and the state is untouched.
#include <doctest/doctest.h>

#include <cstring>
#include <string>

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

TEST_CASE("a machine is started once, and only after it is bound") {
  Fixture fixture;

  SUBCASE("start twice") {
    REQUIRE(fixture.machine.start() == fms::Status::Ok);
    CHECK(fixture.machine.start() == fms::Status::AlreadyInitialised);
    // The second start must not have moved it back to the initial state.
    CHECK(fixture.machine.current() == fixture.standing);
  }
  SUBCASE("rebinding a running machine") {
    REQUIRE(fixture.machine.start() == fms::Status::Ok);
    CHECK(fixture.machine.init(fixture.model, fixture.setup) == fms::Status::AlreadyInitialised);
  }
}

TEST_CASE("an unbound machine cannot be started") {
  fms::StateMachine machine;
  CHECK(machine.start() == fms::Status::NotInitialised);
  CHECK(machine.model() == nullptr);
  CHECK(machine.setup() == nullptr);
  CHECK(machine.current() == fms::kNoState);
  CHECK(std::strcmp(machine.current_name(), "<uninitialised>") == 0);
}

TEST_CASE("the fire overloads are the same decision with less to type") {
  Fixture fixture;
  REQUIRE(fixture.machine.start() == fms::Status::Ok);

  SUBCASE("trigger only") {
    CHECK(fixture.machine.fire(fixture.throttle) == fms::Status::Ok);
    CHECK(fixture.machine.current() == fixture.accelerating);
  }
  SUBCASE("trigger and arguments") {
    fms::Args args;
    REQUIRE(args.parse(sv("pedal=60")) == fms::Status::Ok);
    CHECK(fixture.machine.fire(fixture.throttle, args) == fms::Status::Ok);
    CHECK(fixture.machine.current() == fixture.accelerating);
  }
  SUBCASE("trigger and event") {
    fms::TransitionEvent event;
    CHECK(fixture.machine.fire(fixture.throttle, event) == fms::Status::Ok);
    CHECK(event.accepted);
    CHECK(event.from == fixture.standing);
    CHECK(event.to == fixture.accelerating);
    CHECK(event.trigger == fixture.throttle);
  }
}

TEST_CASE("counters distinguish what moved the machine from what did not") {
  Fixture fixture;
  REQUIRE(fixture.machine.start() == fms::Status::Ok);

  REQUIRE(fixture.machine.fire(fixture.throttle) == fms::Status::Ok);
  CHECK(fixture.machine.fire(fixture.throttle) == fms::Status::NoTransition);
  CHECK(fixture.machine.fire(fixture.brake) == fms::Status::Ok);

  CHECK(fixture.machine.transition_count() == 2);
  CHECK(fixture.machine.rejection_count() == 1);
}

TEST_CASE("the model refuses a nameless state or trigger") {
  fms::Model     model;
  fms::StateId   state   = fms::kNoState;
  fms::TriggerId trigger = fms::kNoTrigger;

  CHECK(model.declare_state(fms::StringView{}, state) == fms::Status::InvalidArgument);
  CHECK(state == fms::kNoState);
  CHECK(model.declare_trigger(fms::StringView{}, fms::StringView{}, trigger) ==
        fms::Status::InvalidArgument);
  CHECK(trigger == fms::kNoTrigger);
}

TEST_CASE("an id nothing was declared under resolves to nothing, not to garbage") {
  Fixture fixture;
  const fms::StateId   no_such_state   = 900;
  const fms::TriggerId no_such_trigger = 900;

  CHECK(!fixture.model.has_state(no_such_state));
  CHECK(fixture.model.state(no_such_state) == nullptr);
  CHECK(fixture.model.trigger(no_such_trigger) == nullptr);
  CHECK(std::strcmp(fixture.model.state_name(no_such_state), "<invalid>") == 0);
  CHECK(std::strcmp(fixture.model.trigger_name(no_such_trigger), "<invalid>") == 0);
  CHECK(!fixture.model.accepts(no_such_state, fixture.throttle));
  CHECK(fixture.model.target_of(no_such_state, fixture.throttle) == fms::kNoState);
}

TEST_CASE("a lookup key too long to be a name cannot match one") {
  Fixture           fixture;
  const std::string long_name(fms::limits::kMaxNameLength + 1, 'x');
  const std::string long_channel(fms::limits::kMaxChannelLength + 1, 'c');
  const auto        view = [](const std::string& s) {
    return fms::StringView(s.data(), s.size());
  };

  CHECK(fixture.model.find_state(view(long_name)) == fms::kNoState);
  CHECK(fixture.model.find_trigger(view(long_name)) == fms::kNoTrigger);
  CHECK(fixture.model.find_trigger_for_channel(view(long_channel)) == fms::kNoTrigger);
  CHECK(fixture.model.set_name(view(long_name)) == fms::Status::NameTooLong);
}
