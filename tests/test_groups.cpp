// SPDX-License-Identifier: MIT
//
// Groups: states that share transitions.  The loader reads them, the model asks
// the state first and its group second, `~self` keeps each member where it is,
// and the linter and the diagram exporter see them as the file wrote them.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "fms/state_machine.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

// Small enough for the smallest configuration tools/capacity_sweep.sh builds:
// eight states, two alternatives per trigger.
constexpr const char* kMachine = R"(
triggers:
  - {name: go}
  - {name: stop}
  - {name: fault}
  - {name: reset}
states:
  - name: idle
    transitions: {go: driving, stop: parked}
  - name: driving
    transitions: {stop: braking}
  - name: braking
    transitions:
      stop: braking
      # its own answer wins over the group's, but only when its guard holds
      fault: {when: "severity == 1", target: braking}
  - name: parked
    transitions:
      # unguarded: the group is never asked on this member
      fault: parked
  - name: broken
    transitions: {reset: idle}
groups:
  - name: moving
    states: [driving, braking, parked]
    transitions:
      fault:
        - {when: "severity >= 2", target: broken}
        - {target: ~self}
)";

struct Loaded {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;
  fms::Status              status;

  explicit Loaded(const char* machine)
      : status(fms::config::load_machine_string(machine, model, diagnostics)) {}

  fms::StateId   state(const char* name) const { return model.find_state(sv(name)); }
  fms::TriggerId trigger(const char* name) const { return model.find_trigger(sv(name)); }
};

fms::Args args(const char* text) {
  fms::Args parsed;
  REQUIRE(parsed.parse(sv(text)) == fms::Status::Ok);
  return parsed;
}

bool has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

/// The first `diagnostics.message` a machine produces, for the refusal cases.
struct Refused {
  fms::Status  status;
  std::string  message;
};

Refused refuse(const char* machine) {
  const Loaded loaded(machine);
  return Refused{loaded.status, loaded.diagnostics.message.c_str()};
}

}  // namespace

// ---------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------

TEST_CASE("a group is loaded with its members and its transitions") {
  const Loaded loaded(kMachine);
  INFO("diagnostic: ", loaded.diagnostics.message.c_str());
  REQUIRE(loaded.status == fms::Status::Ok);

  const fms::GroupId moving = loaded.model.find_group(sv("moving"));
  REQUIRE(moving != fms::kNoGroup);
  CHECK(loaded.model.group_count() == 1);
  CHECK(std::strcmp(loaded.model.group_name(moving), "moving") == 0);

  CHECK(loaded.model.group_of(loaded.state("driving")) == moving);
  CHECK(loaded.model.group_of(loaded.state("braking")) == moving);
  CHECK(loaded.model.group_of(loaded.state("parked")) == moving);
  CHECK(loaded.model.group_of(loaded.state("idle")) == fms::kNoGroup);

  // Stored once, in the group - not copied into each member.
  CHECK(loaded.model.state(loaded.state("driving"))->transitions.size() == 1);
  CHECK(loaded.model.group(moving)->transitions.size() == 1);
}

TEST_CASE("a machine without groups has none") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: a, transitions: {go: a}}
)");
  REQUIRE(loaded.status == fms::Status::Ok);
  CHECK(loaded.model.group_count() == 0);
  CHECK(loaded.model.group_of(loaded.state("a")) == fms::kNoGroup);
}

TEST_CASE("a group the loader cannot make sense of is refused with a reason") {
  SUBCASE("a member that does not exist") {
    const Refused refused = refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{name: g, states: [a, nowhere]}]
)");
    CHECK(refused.status == fms::Status::UnknownState);
    CHECK(has(refused.message, "nowhere"));
  }
  SUBCASE("a state in two groups") {
    const Refused refused = refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups:
  - {name: g, states: [a]}
  - {name: h, states: [a]}
)");
    CHECK(refused.status == fms::Status::DuplicateName);
    CHECK(has(refused.message, "already in group 'g'"));
  }
  SUBCASE("a group named like a state") {
    const Refused refused = refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{name: a, states: [a]}]
)");
    CHECK(refused.status == fms::Status::DuplicateName);
    CHECK(has(refused.message, "already has this name"));
  }
  SUBCASE("a group without members") {
    const Refused refused = refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{name: g, states: []}]
)");
    CHECK(refused.status == fms::Status::SchemaError);
    CHECK(has(refused.message, "non-empty"));
  }
  SUBCASE("a group without a name") {
    CHECK(refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{states: [a]}]
)").status == fms::Status::SchemaError);
  }
  SUBCASE("'groups' that is not a sequence") {
    CHECK(refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: {g: [a]}
)").status == fms::Status::SchemaError);
  }
  SUBCASE("a group transition to a state that does not exist") {
    const Refused refused = refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{name: g, states: [a], transitions: {go: nowhere}}]
)");
    CHECK(refused.status == fms::Status::UnknownState);
  }
  SUBCASE("a group transition on a trigger that is not declared") {
    CHECK(refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{name: g, states: [a], transitions: {jump: a}}]
)").status == fms::Status::UnknownTrigger);
  }
  SUBCASE("'~self' as the name of a group") {
    CHECK(refuse(R"(
triggers: [{name: go}]
states: [{name: a}]
groups: [{name: "~self", states: [a]}]
)").status == fms::Status::SchemaError);
  }
}

// ---------------------------------------------------------------------------
// the decision
// ---------------------------------------------------------------------------

TEST_CASE("every member takes the group's transition, and names the group") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);
  const fms::GroupId moving = loaded.model.find_group(sv("moving"));

  for (const char* member : {"driving", "braking"}) {
    INFO("member ", member);
    fms::StateId   target = fms::kNoState;
    fms::GroupId   via    = fms::kNoGroup;
    const fms::Decision decision = loaded.model.evaluate(
        loaded.state(member), loaded.trigger("fault"), args("severity=3"), target, via);
    CHECK(decision == fms::Decision::Accepted);
    CHECK(target == loaded.state("broken"));
    CHECK(via == moving);
    CHECK(loaded.model.accepts(loaded.state(member), loaded.trigger("fault")));
  }
}

TEST_CASE("a state outside the group does not inherit its transitions") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);

  fms::StateId target = fms::kNoState;
  CHECK(loaded.model.evaluate(loaded.state("idle"), loaded.trigger("fault"), args("severity=3"),
                              target) == fms::Decision::NoTransition);
  CHECK_FALSE(loaded.model.accepts(loaded.state("idle"), loaded.trigger("fault")));
}

TEST_CASE("the state is asked first, and the group only when the state has no answer") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);
  const fms::StateId   braking = loaded.state("braking");
  const fms::TriggerId fault   = loaded.trigger("fault");

  fms::StateId target = fms::kNoState;
  fms::GroupId via    = fms::kNoGroup;

  // The state's own guard holds: the group is not asked.
  CHECK(loaded.model.evaluate(braking, fault, args("severity=1"), target, via) ==
        fms::Decision::Accepted);
  CHECK(target == braking);
  CHECK(via == fms::kNoGroup);

  // The state's own guard does not hold: the group answers.
  CHECK(loaded.model.evaluate(braking, fault, args("severity=2"), target, via) ==
        fms::Decision::Accepted);
  CHECK(target == loaded.state("broken"));
  CHECK(via == loaded.model.find_group(sv("moving")));

  // An unguarded answer of the state's own: the group is never asked.
  CHECK(loaded.model.evaluate(loaded.state("parked"), fault, args("severity=3"), target, via) ==
        fms::Decision::Accepted);
  CHECK(target == loaded.state("parked"));
  CHECK(via == fms::kNoGroup);
}

TEST_CASE("a guard rejection counts the group's alternatives too") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: a, transitions: {go: {when: "x == 1", target: b}}}
  - {name: b}
groups:
  - {name: g, states: [a], transitions: {go: {when: "x == 2", target: b}}}
)");
  REQUIRE(loaded.status == fms::Status::Ok);

  fms::StateId target = fms::kNoState;
  CHECK(loaded.model.evaluate(loaded.state("a"), loaded.trigger("go"), args("x=3"), target) ==
        fms::Decision::GuardRejected);
  CHECK(loaded.model.evaluate(loaded.state("a"), loaded.trigger("go"), args("x=2"), target) ==
        fms::Decision::Accepted);
}

TEST_CASE("'~self' keeps each member where it is") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);

  for (const char* member : {"driving", "braking"}) {
    INFO("member ", member);
    fms::StateId target = fms::kNoState;
    CHECK(loaded.model.evaluate(loaded.state(member), loaded.trigger("fault"), args("severity=0"),
                                target) == fms::Decision::Accepted);
    CHECK(target == loaded.state(member));
  }
}

TEST_CASE("'~self' in a state's own transitions is that state") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: a, transitions: {go: "~self"}}
)");
  REQUIRE(loaded.status == fms::Status::Ok);
  CHECK(loaded.model.target_of(loaded.state("a"), loaded.trigger("go")) == loaded.state("a"));
  CHECK(std::strcmp(loaded.model.state_name(fms::kSelfState), "~self") == 0);
}

TEST_CASE("a state declared as '~self' keeps the meaning it had before the keyword") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: a, transitions: {go: "~self"}}
  - {name: "~self"}
)");
  REQUIRE(loaded.status == fms::Status::Ok);
  const fms::StateId declared = loaded.state("~self");
  REQUIRE(declared != fms::kNoState);
  CHECK(loaded.model.target_of(loaded.state("a"), loaded.trigger("go")) == declared);
}

TEST_CASE("the machine reports which group a transition came from") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);

  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;
  REQUIRE(fms::config::load_setup_string("fsm: {initial: idle}\n", setup, diagnostics) ==
          fms::Status::Ok);

  fms::StateMachine machine;
  REQUIRE(machine.init(loaded.model, setup) == fms::Status::Ok);
  REQUIRE(machine.start() == fms::Status::Ok);

  fms::TransitionEvent event;
  REQUIRE(machine.fire(loaded.trigger("go"), event) == fms::Status::Ok);
  CHECK(event.group == fms::kNoGroup);

  REQUIRE(machine.fire(loaded.trigger("fault"), args("severity=2"), event) == fms::Status::Ok);
  CHECK(event.group == loaded.model.find_group(sv("moving")));
  CHECK(machine.current() == loaded.state("broken"));
}

// ---------------------------------------------------------------------------
// the model's own interface
// ---------------------------------------------------------------------------

TEST_CASE("the model enforces one group per state and one namespace for names") {
  fms::Model     model;
  fms::StateId   a = fms::kNoState;
  fms::GroupId   g = fms::kNoGroup;
  fms::GroupId   h = fms::kNoGroup;
  fms::TriggerId t = fms::kNoTrigger;

  REQUIRE(model.declare_state(sv("a"), a) == fms::Status::Ok);
  REQUIRE(model.declare_trigger(sv("t"), sv(""), t) == fms::Status::Ok);
  REQUIRE(model.declare_group(sv("g"), g) == fms::Status::Ok);
  REQUIRE(model.declare_group(sv("h"), h) == fms::Status::Ok);

  CHECK(model.declare_group(sv("g"), h) == fms::Status::DuplicateName);
  CHECK(model.declare_group(sv("a"), h) == fms::Status::DuplicateName);
  fms::StateId clash = fms::kNoState;
  CHECK(model.declare_state(sv("g"), clash) == fms::Status::DuplicateName);
  CHECK(model.declare_group(sv(""), h) == fms::Status::InvalidArgument);

  CHECK(model.add_to_group(g, a) == fms::Status::Ok);
  CHECK(model.add_to_group(model.find_group(sv("h")), a) == fms::Status::DuplicateName);
  CHECK(model.add_to_group(g, 99) == fms::Status::UnknownState);
  CHECK(model.add_to_group(fms::kNoGroup, a) == fms::Status::InvalidArgument);

  CHECK(model.add_group_transition(g, t, fms::kSelfState) == fms::Status::Ok);
  CHECK(model.add_group_transition(g, t, 99) == fms::Status::UnknownState);
  CHECK(model.add_group_transition(fms::kNoGroup, t, a) == fms::Status::InvalidArgument);
  CHECK(model.validate() == fms::Status::Ok);

  CHECK(std::strcmp(model.group_name(fms::kNoGroup), "<invalid>") == 0);
  CHECK(model.find_group(sv("nowhere")) == fms::kNoGroup);
}

TEST_CASE("the number of groups is a compile-time ceiling") {
  fms::Model model;
  for (std::size_t i = 0; i < fms::limits::kMaxGroups; ++i) {
    char name[8];
    (void)std::snprintf(name, sizeof(name), "g%zu", i);
    fms::GroupId id = fms::kNoGroup;
    REQUIRE(model.declare_group(sv(name), id) == fms::Status::Ok);
  }
  fms::GroupId id = fms::kNoGroup;
  CHECK(model.declare_group(sv("one_more"), id) == fms::Status::CapacityExceeded);
  CHECK(id == fms::kNoGroup);
}

