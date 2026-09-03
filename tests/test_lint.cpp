// SPDX-License-Identifier: MIT
//
// The second pass: what is wrong with a machine the loader accepted.
//
// Every case here is a machine file that loads without complaint, because that
// is the whole point of the linter - if the loader could have caught it, it
// would have.
#include <doctest/doctest.h>

#include <cstring>

#include <etl/array.h>

#include "fms/inspect/lint.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

/// Loads a machine, insists it was valid, and lints it.  `initial` is a state
/// name rather than an id so the cases below read like the files they are.
struct Linted {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;
  fms::lint::Report        report;
  fms::Status              status = fms::Status::Ok;

  Linted(const char* machine, const char* initial) {
    const fms::Status loaded = fms::config::load_machine_string(machine, model, diagnostics);
    INFO("diagnostic: ", diagnostics.message.c_str());
    REQUIRE(loaded == fms::Status::Ok);

    const fms::StateId start =
        (initial == nullptr) ? fms::kNoState : model.find_state(sv(initial));
    if (initial != nullptr) {
      REQUIRE(start != fms::kNoState);
    }
    status = fms::lint::analyse(model, start, report);
  }

  [[nodiscard]] std::size_t count(fms::lint::Check check) const {
    std::size_t total = 0;
    for (const fms::lint::Finding& finding : report) {
      if (finding.check == check) {
        ++total;
      }
    }
    return total;
  }

  [[nodiscard]] const fms::lint::Finding* first(fms::lint::Check check) const {
    for (const fms::lint::Finding& finding : report) {
      if (finding.check == check) {
        return &finding;
      }
    }
    return nullptr;
  }

  /// The rendered text of the first finding of a kind, for the message tests.
  [[nodiscard]] fms::Message text(fms::lint::Check check) const {
    fms::Message message;
    const fms::lint::Finding* finding = first(check);
    if (finding != nullptr) {
      fms::lint::describe(model, *finding, message);
    }
    return message;
  }
};

bool contains(const fms::Message& haystack, const char* needle) {
  return std::strstr(haystack.c_str(), needle) != nullptr;
}

// ---------------------------------------------------------------------------

TEST_CASE("a machine with nothing wrong produces no findings") {
  const Linted linted(R"(
triggers:
  - {name: go}
  - {name: back}
states:
  - name: idle
    transitions: {go: running}
  - name: running
    transitions: {back: idle}
)",
                      "idle");

  CHECK(linted.status == fms::Status::Ok);
  CHECK(linted.report.empty());
  CHECK_FALSE(fms::lint::has_errors(linted.report));
}

TEST_CASE("a state no trigger leads to is unreachable") {
  const Linted linted(R"(
triggers:
  - {name: go}
  - {name: back}
states:
  - name: idle
    transitions: {go: running}
  - name: running
    transitions: {back: idle}
  - name: orphan
    transitions: {go: idle}
)",
                      "idle");

  CHECK(linted.count(fms::lint::Check::UnreachableState) == 1);
  CHECK(fms::lint::severity_of(fms::lint::Check::UnreachableState) == fms::lint::Severity::Error);
  CHECK(fms::lint::has_errors(linted.report));
  CHECK(contains(linted.text(fms::lint::Check::UnreachableState), "orphan"));
}

TEST_CASE("reachability is a question about the setup, so it is skipped without one") {
  const Linted linted(R"(
triggers: [{name: go}]
states:
  - name: idle
    transitions: {go: idle}
  - name: orphan
)",
                      nullptr);

  CHECK(linted.count(fms::lint::Check::UnreachableState) == 0);
}

TEST_CASE("guards are ignored when deciding what is reachable") {
  // `fault` is only entered when a guard holds.  Whether it ever does is a
  // question about the arguments, not about the file - the configuration
  // provides a path, so the state is reachable.
  const Linted linted(R"(
triggers: [{name: check}, {name: reset}]
states:
  - name: idle
    transitions:
      check:
        - {when: "errors > 0", target: fault}
        - {target: idle}
  - name: fault
    transitions: {reset: idle}
)",
                      "idle");

  CHECK(linted.report.empty());
}

TEST_CASE("a state nothing leads out of is a dead end, but only a warning") {
  const Linted linted(R"(
triggers: [{name: go}, {name: shrug}]
states:
  - name: idle
    transitions: {go: stuck}
  - name: stuck
    transitions: {shrug: stuck}
)",
                      "idle");

  // `stuck` lists a transition, but it comes back to itself.
  CHECK(linted.count(fms::lint::Check::DeadEndState) == 1);
  CHECK(fms::lint::severity_of(fms::lint::Check::DeadEndState) == fms::lint::Severity::Warning);
  CHECK_FALSE(fms::lint::has_errors(linted.report));
  CHECK(contains(linted.text(fms::lint::Check::DeadEndState), "stuck"));
}

TEST_CASE("a trigger no state lists is reported") {
  const Linted linted(R"(
triggers:
  - {name: go}
  - {name: forgotten}
states:
  - name: idle
    transitions: {go: idle}
)",
                      "idle");

  CHECK(linted.count(fms::lint::Check::UnusedTrigger) == 1);
  CHECK(contains(linted.text(fms::lint::Check::UnusedTrigger), "forgotten"));
}

TEST_CASE("nothing after an unguarded alternative can be reached") {
  const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {target: idle}
        - {when: "pedal > 5", target: moving}
  - name: moving
    transitions: {press: idle}
)",
                      "idle");

  const fms::lint::Finding* finding = linted.first(fms::lint::Check::UnreachableAlternative);
  REQUIRE(finding != nullptr);
  CHECK(finding->alternative == 2);
  CHECK(finding->other == 1);  // the unguarded one that swallows it
  CHECK(fms::lint::has_errors(linted.report));
}

TEST_CASE("a guard whose conditions contradict each other can never hold") {
  SUBCASE("two numeric bounds that do not overlap") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["pedal > 60", "pedal < 5"], target: idle}
)",
                        "idle");
    CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
    CHECK(contains(linted.text(fms::lint::Check::ImpossibleGuard), "pedal > 60"));
    CHECK(contains(linted.text(fms::lint::Check::ImpossibleGuard), "pedal < 5"));
  }

  SUBCASE("a value required to be two different numbers") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["gear == 1", "gear == 2"], target: idle}
)",
                        "idle");
    CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  }

  SUBCASE("a value required to be what it must not be") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["gear == 1", "gear != 1"], target: idle}
)",
                        "idle");
    CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  }

  SUBCASE("every value the range allows is excluded by name") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["gear >= 1", "gear <= 1", "gear != 1"], target: idle}
)",
                        "idle");
    CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  }

  SUBCASE("two different words") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["mode == sport", "mode == eco"], target: idle}
)",
                        "idle");
    CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  }

  SUBCASE("a word and a number for the same argument") {
    // A numeric comparison is false unless the value parses as an integer, and
    // `sport` never will - so the two can never hold together.
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["mode == sport", "mode > 2"], target: idle}
)",
                        "idle");
    CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  }
}

TEST_CASE("conditions on different arguments never contradict each other") {
  const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["pedal > 60", "speed < 5"], target: moving}
        - {target: idle}
  - name: moving
    transitions: {press: idle}
)",
                      "idle");

  CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 0);
}

TEST_CASE("overlapping ranges are possible and are left alone") {
  const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["pedal > 5", "pedal <= 60"], target: idle}
)",
                      "idle");

  CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 0);
}

TEST_CASE("an alternative an earlier one always beats is dead") {
  SUBCASE("the same guard twice") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: "pedal > 5", target: moving}
        - {when: "pedal > 5", target: idle}
  - name: moving
    transitions: {press: idle}
)",
                        "idle");

    const fms::lint::Finding* finding = linted.first(fms::lint::Check::ShadowedAlternative);
    REQUIRE(finding != nullptr);
    CHECK(finding->alternative == 2);
    CHECK(finding->other == 1);
  }

  SUBCASE("an earlier guard that asks for less") {
    // The second needs both conditions; the first needs one of them, so the
    // first holds every time the second would.
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: "pedal > 5", target: moving}
        - {when: ["pedal > 5", "mode == sport"], target: idle}
  - name: moving
    transitions: {press: idle}
)",
                        "idle");

    CHECK(linted.count(fms::lint::Check::ShadowedAlternative) == 1);
  }

  SUBCASE("a narrower guard first is not shadowing") {
    const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: ["pedal > 5", "mode == sport"], target: moving}
        - {when: "pedal > 5", target: idle}
  - name: moving
    transitions: {press: idle}
)",
                        "idle");

    CHECK(linted.count(fms::lint::Check::ShadowedAlternative) == 0);
  }
}

TEST_CASE("one alternative produces one finding, and the deepest one wins") {
  // The third alternative is both unreachable and impossible.  Only the
  // unreachability is reported: fixing that is what makes the guard matter.
  const Linted linted(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press:
        - {when: "pedal > 5", target: idle}
        - {target: idle}
        - {when: ["pedal > 60", "pedal < 5"], target: idle}
)",
                      "idle");

  CHECK(linted.count(fms::lint::Check::UnreachableAlternative) == 1);
  CHECK(linted.count(fms::lint::Check::ImpossibleGuard) == 0);
}

TEST_CASE("a full report says so rather than pretending it looked at everything") {
  // Built through the Model interface: a machine with this many findings is
  // hard to write in YAML and pointless to read.
  fms::Model model;
  for (std::size_t i = 0; i < fms::limits::kMaxStates; ++i) {
    etl::array<char, 8> name{};
    const int           written = std::snprintf(name.data(), name.size(), "s%zu", i);
    REQUIRE(written > 0);

    fms::StateId id = fms::kNoState;
    REQUIRE(model.declare_state(fms::StringView(name.data(), static_cast<std::size_t>(written)),
                                id) == fms::Status::Ok);
  }
  fms::TriggerId trigger = fms::kNoTrigger;
  REQUIRE(model.declare_trigger(sv("unused"), fms::StringView{}, trigger) == fms::Status::Ok);

  // Every state is a dead end, every state but the first is unreachable, and
  // the trigger is unused: two findings per state.
  constexpr std::size_t kFindings = 2 * fms::limits::kMaxStates;

  fms::lint::Report report;
  // Both sides are capacities, so which branch this test is depends on the
  // configuration and on nothing that happens at run time.  `if constexpr`
  // says that; a plain `if` is MSVC's C4127, and it has a point.
  if constexpr (kFindings > fms::limits::kMaxFindings) {
    CHECK(fms::lint::analyse(model, 0, report) == fms::Status::CapacityExceeded);
    CHECK(report.size() == fms::limits::kMaxFindings);
    CHECK(report.full());
  } else {
    // A tree configured with few states and a large report cannot overflow one;
    // there the contract is that nothing was dropped.  It can still end up
    // exactly full - kMaxStates 16 against kMaxFindings 32 - and a report that
    // is full without having dropped anything is not a capacity error.
    CHECK(fms::lint::analyse(model, 0, report) == fms::Status::Ok);
    CHECK(report.size() == kFindings);
    CHECK(report.full() == (kFindings == fms::limits::kMaxFindings));
  }
}

TEST_CASE("every check has a slug and a severity") {
  const etl::array<fms::lint::Check, 6> checks = {
      fms::lint::Check::UnreachableState,       fms::lint::Check::DeadEndState,
      fms::lint::Check::UnusedTrigger,          fms::lint::Check::UnreachableAlternative,
      fms::lint::Check::ImpossibleGuard,        fms::lint::Check::ShadowedAlternative,
  };
  for (const fms::lint::Check check : checks) {
    CHECK(std::strlen(fms::lint::to_string(check)) > 0);
    CHECK(std::strcmp(fms::lint::to_string(check), "unknown-check") != 0);
  }
  CHECK(std::strcmp(fms::lint::to_string(fms::lint::Severity::Error), "error") == 0);
  CHECK(std::strcmp(fms::lint::to_string(fms::lint::Severity::Warning), "warning") == 0);
}

TEST_CASE("the shipped car machine is clean") {
  fms::Model               model;
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  REQUIRE(fms::config::load_setup_file(FMS_SOURCE_DIR "/examples/car/car.setup.yaml", setup,
                                       diagnostics) == fms::Status::Ok);
  REQUIRE(fms::config::load_machine_file(FMS_SOURCE_DIR "/examples/car/car.machine.yaml", model,
                                         diagnostics) == fms::Status::Ok);

  fms::lint::Report report;
  CHECK(fms::lint::analyse(model, setup.initial_in(model), report) == fms::Status::Ok);

  fms::Message text;
  for (const fms::lint::Finding& finding : report) {
    fms::lint::describe(model, finding, text);
    INFO("unexpected finding: ", text.c_str());
    CHECK(false);
  }
  CHECK(report.empty());
}

}  // namespace

TEST_CASE("an impossible guard is described with the conditions that contradict") {
  // The finding names coordinates; describe() has to resolve them back into the
  // guard as it was written, or the reader has to go and find it themselves.
  const Linted linted(R"(
triggers:
  - {name: go}
states:
  - name: idle
    transitions:
      go:
        - {when: ["pedal > 60", "pedal < 5"], target: moving}
        - {target: idle}
  - name: moving
    transitions:
      go: idle
)",
                      "idle");

  REQUIRE(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  const fms::Message message = linted.text(fms::lint::Check::ImpossibleGuard);

  CHECK(std::strstr(message.c_str(), "state 'idle'") != nullptr);
  CHECK(std::strstr(message.c_str(), "trigger 'go'") != nullptr);
  CHECK(std::strstr(message.c_str(), "alternative 1") != nullptr);
  CHECK(std::strstr(message.c_str(), "can never hold") != nullptr);
  // Both halves of the contradiction, joined - one alone would not explain it.
  CHECK(std::strstr(message.c_str(), "pedal > 60") != nullptr);
  CHECK(std::strstr(message.c_str(), "pedal < 5") != nullptr);
  CHECK(std::strstr(message.c_str(), " and ") != nullptr);
}

TEST_CASE("a shadowed alternative is described by the one that beats it") {
  const Linted linted(R"(
triggers:
  - {name: go}
states:
  - name: idle
    transitions:
      go:
        - {when: "pedal > 5", target: moving}
        - {when: ["pedal > 5", "gear == 1"], target: idle}
        - {target: idle}
  - name: moving
    transitions:
      go: idle
)",
                      "idle");

  REQUIRE(linted.count(fms::lint::Check::ShadowedAlternative) == 1);
  const fms::lint::Finding* finding = linted.first(fms::lint::Check::ShadowedAlternative);
  REQUIRE(finding != nullptr);
  CHECK(finding->alternative == 2);
  CHECK(finding->other == 1);

  const fms::Message message = linted.text(fms::lint::Check::ShadowedAlternative);
  CHECK(std::strstr(message.c_str(), "alternative 2") != nullptr);
  CHECK(std::strstr(message.c_str(), "alternative 1") != nullptr);
  CHECK(std::strstr(message.c_str(), "holds whenever this one would") != nullptr);
}

TEST_CASE("a text guard that cannot hold is described with its words") {
  const Linted linted(R"(
triggers:
  - {name: go}
states:
  - name: idle
    transitions:
      go:
        - {when: ["mode == sport", "mode != sport"], target: moving}
        - {target: idle}
  - name: moving
    transitions:
      go: idle
)",
                      "idle");

  REQUIRE(linted.count(fms::lint::Check::ImpossibleGuard) == 1);
  const fms::Message message = linted.text(fms::lint::Check::ImpossibleGuard);
  CHECK(std::strstr(message.c_str(), "mode == sport") != nullptr);
  CHECK(std::strstr(message.c_str(), "mode != sport") != nullptr);
}
