// SPDX-License-Identifier: MIT
//
// The diagram is generated from the Model, so these tests are about one thing:
// does the picture say what the file says.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "fms/inspect/diagram.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

/// The sink a caller who does want the whole diagram in memory would write.
struct Collected {
  std::string text;

  static void sink(void* user, fms::StringView fragment) {
    static_cast<Collected*>(user)->text.append(fragment.data(), fragment.size());
  }
};

bool has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string render(const char* machine, fms::diagram::Format format, const char* initial) {
  fms::Model               model;
  fms::config::Diagnostics diagnostics;

  const fms::Status loaded = fms::config::load_machine_string(machine, model, diagnostics);
  INFO("diagnostic: ", diagnostics.message.c_str());
  REQUIRE(loaded == fms::Status::Ok);

  const fms::StateId start = (initial == nullptr) ? fms::kNoState : model.find_state(sv(initial));

  Collected collected;
  REQUIRE(fms::diagram::render(model, start, format, &Collected::sink, &collected) ==
          fms::Status::Ok);
  return collected.text;
}

constexpr const char* kMachine = R"(
fsm: {name: demo}
triggers:
  - {name: press}
  - {name: release}
states:
  - name: idle
    transitions:
      press:
        - {when: "pedal > 5", target: moving}
        - {target: idle}
  - name: moving
    transitions:
      release: idle
)";

// ---------------------------------------------------------------------------

TEST_CASE("mermaid: one edge per alternative, labelled with its guard") {
  const std::string text = render(kMachine, fms::diagram::Format::Mermaid, "idle");

  CHECK(has(text, "stateDiagram-v2\n"));
  CHECK(has(text, "    [*] --> idle\n"));
  CHECK(has(text, "    idle --> moving: press [pedal #gt; 5]\n"));
  CHECK(has(text, "    moving --> idle: release\n"));
}

TEST_CASE("mermaid: the fallback is named, because a picture cannot show file order") {
  const std::string text = render(kMachine, fms::diagram::Format::Mermaid, "idle");
  CHECK(has(text, "    idle --> idle: press [otherwise]\n"));
}

TEST_CASE("mermaid: an unguarded trigger with no siblings is just its name") {
  const std::string text = render(kMachine, fms::diagram::Format::Mermaid, "idle");
  CHECK_FALSE(has(text, "release [otherwise]"));
}

TEST_CASE("mermaid: angle brackets are escaped, or the label would be read as a tag") {
  const std::string text = render(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press: {when: "pedal < 5", target: idle}
)",
                                  fms::diagram::Format::Mermaid, "idle");

  CHECK(has(text, "[pedal #lt; 5]"));
  CHECK_FALSE(has(text, "pedal < 5"));
}

TEST_CASE("mermaid: ANDed conditions are spelled out in one label") {
  const std::string text = render(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press: {when: ["pedal > 5", "mode == sport"], target: idle}
)",
                                  fms::diagram::Format::Mermaid, "idle");

  CHECK(has(text, "[pedal #gt; 5 and mode == sport]"));
}

TEST_CASE("without an initial state there is no entry arrow") {
  const std::string text = render(kMachine, fms::diagram::Format::Mermaid, nullptr);
  CHECK_FALSE(has(text, "[*]"));
  CHECK(has(text, "idle --> moving"));
}

TEST_CASE("dot: a digraph named after the machine") {
  const std::string text = render(kMachine, fms::diagram::Format::Dot, "idle");

  CHECK(has(text, "digraph \"demo\" {"));
  CHECK(has(text, "__start -> \"idle\";"));
  CHECK(has(text, "\"idle\" -> \"moving\" [label=\"press\\n[pedal > 5]\"];"));
  CHECK(has(text, "\"moving\" -> \"idle\" [label=\"release\"];"));
  CHECK(has(text, "}\n"));
}

TEST_CASE("dot: an unnamed machine still produces a valid digraph") {
  const std::string text = render(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions: {press: idle}
)",
                                  fms::diagram::Format::Dot, "idle");

  CHECK(has(text, "digraph \"machine\" {"));
}

TEST_CASE("dot: a quote in a name is escaped rather than closing the label") {
  const std::string text = render(R"(
fsm: {name: 'sa"y'}
triggers: [{name: press}]
states:
  - name: idle
    transitions: {press: idle}
)",
                                  fms::diagram::Format::Dot, "idle");

  CHECK(has(text, "digraph \"sa\\\"y\" {"));
}

TEST_CASE("negative literals survive the round trip") {
  const std::string text = render(R"(
triggers: [{name: press}]
states:
  - name: idle
    transitions:
      press: {when: "offset >= -2147483648", target: idle}
)",
                                  fms::diagram::Format::Mermaid, "idle");

  CHECK(has(text, "[offset #gt;= -2147483648]"));
}

TEST_CASE("a format name is parsed, or refused") {
  fms::diagram::Format format = fms::diagram::Format::Dot;

  CHECK(fms::diagram::parse_format(sv("mermaid"), format));
  CHECK(format == fms::diagram::Format::Mermaid);
  CHECK(fms::diagram::parse_format(sv("dot"), format));
  CHECK(format == fms::diagram::Format::Dot);

  CHECK_FALSE(fms::diagram::parse_format(sv("svg"), format));
  CHECK_FALSE(fms::diagram::parse_format(fms::StringView{}, format));
  CHECK(format == fms::diagram::Format::Dot);  // untouched when it does not parse
}

TEST_CASE("no sink is an argument error, not a crash") {
  const fms::Model model;
  CHECK(fms::diagram::render(model, fms::kNoState, fms::diagram::Format::Mermaid, nullptr,
                             nullptr) == fms::Status::InvalidArgument);
}

TEST_CASE("the shipped car machine renders every alternative") {
  fms::Model               model;
  fms::Setup               setup;
  fms::config::Diagnostics diagnostics;

  REQUIRE(fms::config::load_setup_file(FMS_SOURCE_DIR "/examples/car/car.setup.yaml", setup,
                                       diagnostics) == fms::Status::Ok);
  REQUIRE(fms::config::load_machine_file(FMS_SOURCE_DIR "/examples/car/car.machine.yaml", model,
                                         diagnostics) == fms::Status::Ok);

  Collected collected;
  REQUIRE(fms::diagram::render(model, setup.initial_in(model), fms::diagram::Format::Mermaid,
                               &Collected::sink, &collected) == fms::Status::Ok);

  // One line per alternative, plus the header and the entry arrow.
  std::size_t alternatives = 0;
  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      alternatives += transition.second.size();
    }
  }
  const std::size_t lines =
      static_cast<std::size_t>(std::count(collected.text.begin(), collected.text.end(), '\n'));

  CHECK(lines == alternatives + 2);
  CHECK(has(collected.text, "    [*] --> power_off\n"));
  CHECK(has(collected.text, "    self_test --> standing: self_test_passed [errors == 0]\n"));
  CHECK(has(collected.text, "    self_test --> fault: self_test_passed [otherwise]\n"));
}

}  // namespace
