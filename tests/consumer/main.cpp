// SPDX-License-Identifier: MIT
//
// Loads a machine through the installed package and reports what it found.
// Deliberately uses three of the installed targets - the core, the loader and
// the linter - so a missing export or a missing transitive dependency shows up
// here rather than in someone else's project.
//
//   consumer <setup.yaml> <machine.yaml>
#include <cstdio>

#include <fms/abi.hpp>
#include <fms/inspect/lint.hpp>
#include <fms/state_machine.hpp>
#include <fms/version.hpp>
#include <fms/yaml_loader.hpp>

namespace {
fms::Setup g_setup;
fms::Model g_model;
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    (void)std::fputs("usage: consumer <setup.yaml> <machine.yaml>\n", stderr);
    return 2;
  }

  fms::config::Diagnostics diagnostics;
  if (!fms::is_ok(fms::config::load_setup_file(argv[1], g_setup, diagnostics)) ||
      !fms::is_ok(fms::config::load_machine_file(argv[2], g_model, diagnostics))) {
    (void)std::fprintf(stderr, "line %d: %s\n", diagnostics.line, diagnostics.message.c_str());
    return 1;
  }

  fms::StateMachine machine;
  if (!fms::is_ok(machine.init(g_model, g_setup))) {
    (void)std::fputs("the setup does not fit the machine\n", stderr);
    return 1;
  }

  fms::lint::Report report;
  (void)fms::lint::analyse(g_model, g_setup.initial_in(g_model), report);

  (void)std::printf("fms-yaml %s (capacities %s): %zu states, %zu triggers, %zu finding(s)\n",
                    fms::version(), fms::abi::tag(), g_model.state_count(),
                    g_model.trigger_count(), report.size());
  return 0;
}
