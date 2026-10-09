// SPDX-License-Identifier: MIT
//
// Groups as the linter and the diagram exporter see them.  The loading and the
// decision are in test_groups.cpp; these need fms_inspect.
#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "fms/inspect/diagram.hpp"
#include "fms/inspect/lint.hpp"
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

struct Collected {
  std::string text;

  static void sink(void* user, fms::StringView fragment) {
    static_cast<Collected*>(user)->text.append(fragment.data(), fragment.size());
  }
};

std::string render(const fms::Model& model, fms::diagram::Format format) {
  Collected out;
  REQUIRE(fms::diagram::render(model, model.find_state(sv("idle")), format, &Collected::sink,
                               &out) == fms::Status::Ok);
  return out.text;
}

bool has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ---------------------------------------------------------------------------
// the linter
// ---------------------------------------------------------------------------

namespace {

struct Linted {
  Loaded            loaded;
  fms::lint::Report report;

  Linted(const char* machine, const char* initial) : loaded(machine) {
    INFO("diagnostic: ", loaded.diagnostics.message.c_str());
    REQUIRE(loaded.status == fms::Status::Ok);
    REQUIRE(fms::lint::analyse(loaded.model, loaded.state(initial), report) == fms::Status::Ok);
  }

  std::size_t count(fms::lint::Check check) const {
    std::size_t found = 0;
    for (const fms::lint::Finding& finding : report) {
      found += (finding.check == check) ? 1U : 0U;
    }
    return found;
  }

  const fms::lint::Finding* first(fms::lint::Check check) const {
    for (const fms::lint::Finding& finding : report) {
      if (finding.check == check) {
        return &finding;
      }
    }
    return nullptr;
  }
};

}  // namespace

TEST_CASE("the shared example lints clean") {
  const Linted linted(kMachine, "idle");
  CHECK(linted.report.empty());
}

TEST_CASE("a state reached only through a group transition is reachable") {
  const Linted linted(R"(
triggers: [{name: go}, {name: fail}]
states:
  - {name: a, transitions: {go: b}}
  - {name: b, transitions: {go: a}}
  - {name: broken, transitions: {go: a}}
groups:
  - {name: g, states: [a, b], transitions: {fail: broken}}
)",
                      "a");
  CHECK(linted.count(fms::lint::Check::UnreachableState) == 0);
  CHECK(linted.count(fms::lint::Check::UnusedTrigger) == 0);
}

TEST_CASE("a state whose only way out is its group's is not a dead end") {
  const Linted linted(R"(
triggers: [{name: go}, {name: fail}]
states:
  - {name: a, transitions: {go: b}}
  - {name: b}
  - {name: broken, transitions: {go: a}}
groups:
  - {name: g, states: [a, b], transitions: {fail: broken}}
)",
                      "a");
  CHECK(linted.count(fms::lint::Check::DeadEndState) == 0);
}

TEST_CASE("'~self' does not lead anywhere") {
  const Linted linted(R"(
triggers: [{name: go}, {name: stay}]
states:
  - {name: a, transitions: {go: b}}
  - {name: b}
groups:
  - {name: g, states: [b], transitions: {stay: "~self"}}
)",
                      "a");
  REQUIRE(linted.count(fms::lint::Check::DeadEndState) == 1);
  CHECK(linted.first(fms::lint::Check::DeadEndState)->state == linted.loaded.state("b"));
}

TEST_CASE("a group transition every member answers first is reported") {
  const Linted linted(R"(
triggers: [{name: go}, {name: fail}]
states:
  - {name: a, transitions: {go: b, fail: a}}
  - {name: b, transitions: {go: a, fail: b}}
  - {name: broken, transitions: {go: a}}
groups:
  - {name: g, states: [a, b], transitions: {fail: broken}}
)",
                      "a");
  REQUIRE(linted.count(fms::lint::Check::OverriddenGroupTransition) == 1);
  const fms::lint::Finding* finding = linted.first(fms::lint::Check::OverriddenGroupTransition);
  CHECK(finding->group == linted.loaded.model.find_group(sv("g")));
  CHECK(finding->trigger == linted.loaded.trigger("fail"));
  CHECK(fms::lint::severity_of(finding->check) == fms::lint::Severity::Error);

  fms::Message text;
  fms::lint::describe(linted.loaded.model, *finding, text);
  CHECK(std::strncmp(text.c_str(), "group 'g', trigger 'fail'", 25) == 0);
}

TEST_CASE("a member that answers with a guard still leaves the group reachable") {
  const Linted linted(R"(
triggers: [{name: go}, {name: fail}]
states:
  - {name: a, transitions: {go: b, fail: {when: "x == 1", target: a}}}
  - {name: b, transitions: {go: a, fail: b}}
  - {name: broken, transitions: {go: a}}
groups:
  - {name: g, states: [a, b], transitions: {fail: broken}}
)",
                      "a");
  CHECK(linted.count(fms::lint::Check::OverriddenGroupTransition) == 0);
}

TEST_CASE("a group's alternatives get the same checks as a state's, under the group's name") {
  const Linted linted(R"(
triggers: [{name: go}]
states:
  - {name: a, transitions: {go: b}}
  - {name: b, transitions: {go: a}}
groups:
  - name: g
    states: [b]
    transitions:
      go:
        - {target: a}
        - {target: b}
)",
                      "a");
  REQUIRE(linted.count(fms::lint::Check::UnreachableAlternative) == 1);
  const fms::lint::Finding* finding = linted.first(fms::lint::Check::UnreachableAlternative);
  CHECK(finding->group == linted.loaded.model.find_group(sv("g")));
  CHECK(finding->state == fms::kNoState);

  fms::Message text;
  fms::lint::describe(linted.loaded.model, *finding, text);
  CHECK(std::strncmp(text.c_str(), "group 'g', trigger 'go', alternative 2", 38) == 0);
}

TEST_CASE("an impossible guard in a group names the guard") {
  const Linted linted(R"(
triggers: [{name: go}]
states:
  - {name: a, transitions: {go: b}}
  - {name: b, transitions: {go: a}}
groups:
  - {name: g, states: [b], transitions: {go: {when: ["x > 5", "x < 2"], target: a}}}
)",
                      "a");
  REQUIRE(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  fms::Message text;
  fms::lint::describe(linted.loaded.model,
                      *linted.first(fms::lint::Check::ImpossibleGuard), text);
  // Clipped in a build with a short FMS_MAX_MESSAGE_LENGTH; the guard is last.
  if (text.size() < fms::limits::kMaxMessageLength) {
    CHECK(has(text.c_str(), "x > 5 and x < 2"));
  }
}

// ---------------------------------------------------------------------------
// the diagram
// ---------------------------------------------------------------------------

TEST_CASE("mermaid: a group is a composite state, and its transitions leave it once") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);
  const std::string text = render(loaded.model, fms::diagram::Format::Mermaid);

  CHECK(has(text, "    state moving {\n        driving\n        braking\n        parked\n    }\n"));
  CHECK(has(text, "    moving --> broken: fault [severity #gt;= 2]\n"));
  CHECK(has(text, "    moving --> moving: fault [otherwise]\n"));
  // Not drawn per member: the group's edge is the only one leading to broken.
  CHECK_FALSE(has(text, "driving --> broken"));
  CHECK_FALSE(has(text, "braking --> broken"));
  // A member's own transitions are still its own.
  CHECK(has(text, "    braking --> braking: fault [severity == 1]\n"));
}

TEST_CASE("mermaid: '~self' in a state is a loop on that state") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: idle, transitions: {go: "~self"}}
)");
  REQUIRE(loaded.status == fms::Status::Ok);
  const std::string text = render(loaded.model, fms::diagram::Format::Mermaid);
  CHECK(has(text, "    idle --> idle: go\n"));
  CHECK_FALSE(has(text, "~self"));
}

TEST_CASE("mermaid: a machine without groups draws no composite state") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: idle, transitions: {go: idle}}
)");
  REQUIRE(loaded.status == fms::Status::Ok);
  CHECK(render(loaded.model, fms::diagram::Format::Mermaid) ==
        "stateDiagram-v2\n    [*] --> idle\n    idle --> idle: go\n");
}

TEST_CASE("dot: a group is a cluster, and its transitions leave the cluster's border") {
  const Loaded loaded(kMachine);
  REQUIRE(loaded.status == fms::Status::Ok);
  const std::string text = render(loaded.model, fms::diagram::Format::Dot);

  CHECK(has(text, "  compound=true;\n"));
  CHECK(has(text, "  subgraph \"cluster_moving\" {\n"));
  // `~self` cannot be an edge from a cluster to itself; it is a label line.
  CHECK(has(text, "    label=\"moving\\nfault [otherwise] / stay\";\n"));
  CHECK(has(text, "    \"driving\";\n    \"braking\";\n    \"parked\";\n"));
  CHECK(has(text,
            "  \"driving\" -> \"broken\" [ltail=\"cluster_moving\", "
            "label=\"fault\\n[severity >= 2]\"];\n"));
}

TEST_CASE("dot: a machine without groups is not a compound graph") {
  const Loaded loaded(R"(
triggers: [{name: go}]
states:
  - {name: idle, transitions: {go: idle}}
)");
  REQUIRE(loaded.status == fms::Status::Ok);
  const std::string text = render(loaded.model, fms::diagram::Format::Dot);
  CHECK_FALSE(has(text, "compound"));
  CHECK_FALSE(has(text, "subgraph"));
}
