// SPDX-License-Identifier: MIT
//
// Loads a machine through the installed package and reports what it found.
// Deliberately uses three of the installed targets - the core, the loader and
// the linter - so a missing export or a missing transitive dependency shows up
// here rather than in someone else's project.
//
//   consumer <setup.yaml> <machine.yaml>
#include <iostream>

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
    std::cerr << "usage: consumer <setup.yaml> <machine.yaml>\n";
    return 2;
  }

  fms::config::Diagnostics diagnostics;
  if (!fms::is_ok(fms::config::load_setup_file(argv[1], g_setup, diagnostics)) ||
      !fms::is_ok(fms::config::load_machine_file(argv[2], g_model, diagnostics))) {
    std::cerr << "line " << diagnostics.line << ": " << diagnostics.message.c_str() << '\n';
    return 1;
  }

  fms::StateMachine machine;
  if (!fms::is_ok(machine.init(g_model, g_setup))) {
    std::cerr << "the setup does not fit the machine\n";
    return 1;
  }

  fms::lint::Report report;
  (void)fms::lint::analyse(g_model, g_setup.initial_in(g_model), report);

  std::cout << "fms-yaml " << fms::version() << " (capacities " << fms::abi::tag()
            << "): " << g_model.state_count() << " states, " << g_model.trigger_count()
            << " triggers, " << report.size() << " finding(s)\n";
  return 0;
}
