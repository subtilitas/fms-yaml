// SPDX-License-Identifier: MIT
//
// Car state machine on the console.
//
//   ./car_console [setup.yaml] [machine.yaml] [--quiet] [--check]
//
// The YAML is read here, when the program runs, and nowhere else: the build does
// not copy it, parse it or depend on it.  So the paths are yours to choose -
// they default to car.setup.yaml and car.machine.yaml in the working directory:
//
//   cd examples/car && ../../build/car_console
//   ./build/car_console examples/car/car.setup.yaml examples/car/car.machine.yaml
//
// --check loads both files, reports what they describe and exits, which is how
// you validate a configuration without building anything.
//
// The configuration comes in two files: the setup says where this instance runs
// and where it starts, the machine says what it does.  Point the same machine
// file at a different setup and you have a second deployment.
//
// Type a trigger name and press enter; "help" lists them, "quit" ends the
// session.  It pipes just as well:
//
//   printf 'ignition_on\nself_test_passed\n' | ./car_console --quiet
//
// There is nothing to register: the machine is entirely described by the YAML,
// and the only application code is the trace hook below.
#include <cstdio>
#include <cstring>

#include "fms/port/console_port.hpp"
#include "fms/runtime.hpp"
#include "fms/yaml_loader.hpp"

namespace {

void trace(void* user, const fms::TransitionEvent& event) {
  const auto* model = static_cast<const fms::Model*>(user);

  if (event.accepted) {
    std::fprintf(stderr, "  [%s --%s--> %s]\n", model->state_name(event.from),
                 model->trigger_name(event.trigger), model->state_name(event.to));
  }
}

/// Reports a failed load with the file it came from, so a two-file setup does
/// not leave you guessing which one is wrong.
void report(const char* path, fms::Status status, const fms::config::Diagnostics& diagnostics) {
  std::fprintf(stderr, "%s: %s", path, fms::to_string(status));
  if (diagnostics.line > 0) {
    std::fprintf(stderr, " at line %d", diagnostics.line);
  }
  std::fprintf(stderr, ": %s\n", diagnostics.message.c_str());
  if (status == fms::Status::FileNotFound) {
    std::fputs("hint: pass the paths, e.g. examples/car/car.setup.yaml "
               "examples/car/car.machine.yaml\n",
               stderr);
  }
}

/// --check: say what the two files describe, then stop.
void describe(const fms::Setup& setup, const fms::Model& model, const fms::StateMachine& machine) {
  std::printf("setup   : instance '%s', starts in '%s'\n", setup.name().c_str(),
              setup.initial_name().c_str());
  std::printf("machine : '%s', %zu states, %zu triggers, %zu guard conditions\n",
              model.name().c_str(), model.state_count(), model.trigger_count(),
              model.condition_count());

  for (const auto& entry : model.states()) {
    const fms::StateId id = entry.first;
    std::printf("  %-13s %s%zu trigger(s)", model.state_name(id),
                (id == machine.initial()) ? "* " : "  ", entry.second.transitions.size());
    for (const auto& transition : entry.second.transitions) {
      std::printf(" %s", model.trigger_name(transition.first));
      if (transition.second.size() > 1) {
        std::printf("(%zu)", transition.second.size());
      }
    }
    std::putchar('\n');
  }
  std::puts("ok");
}

// The whole system, statically allocated.
fms::Setup        g_setup;
fms::Model        g_model;
fms::StateMachine g_machine;
fms::Runtime      g_runtime;

}  // namespace

int main(int argc, char** argv) {
  const char* setup_path   = "car.setup.yaml";
  const char* machine_path = "car.machine.yaml";
  bool        quiet        = false;
  bool        check_only   = false;
  int         positional   = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quiet") == 0) {
      quiet = true;
    } else if (std::strcmp(argv[i], "--check") == 0) {
      check_only = true;
    } else if (positional == 0) {
      setup_path = argv[i];
      ++positional;
    } else {
      machine_path = argv[i];
      ++positional;
    }
  }

  // ---- setup phase: the only place that may allocate ----------------------
  fms::config::Diagnostics diagnostics;

  const fms::Status setup_loaded =
      fms::config::load_setup_file(setup_path, g_setup, diagnostics);
  if (!fms::is_ok(setup_loaded)) {
    report(setup_path, setup_loaded, diagnostics);
    return 1;
  }

  const fms::Status machine_loaded =
      fms::config::load_machine_file(machine_path, g_model, diagnostics);
  if (!fms::is_ok(machine_loaded)) {
    report(machine_path, machine_loaded, diagnostics);
    return 1;
  }

  // init() is where the two files have to agree: the initial state named by the
  // setup must exist in the machine.
  const fms::Status bound = g_machine.init(g_model, g_setup);
  if (!fms::is_ok(bound)) {
    std::fprintf(stderr, "%s does not fit %s: %s ('%s')\n", setup_path, machine_path,
                 fms::to_string(bound), g_setup.initial_name().c_str());
    return 1;
  }

  if (check_only) {
    describe(g_setup, g_model, g_machine);
    return 0;
  }

  fms::port::ConsolePort port(/*prompt=*/!quiet);

  if (!fms::is_ok(g_runtime.init(g_machine, port))) {
    std::fputs("runtime init failed\n", stderr);
    return 1;
  }

  if (!quiet) {
    g_runtime.set_trace(&trace, &g_model);
    std::printf("%s running '%s': %zu states, %zu triggers - type 'help' or 'quit'\n",
                g_setup.name().c_str(), g_model.name().c_str(), g_model.state_count(),
                g_model.trigger_count());
  }

  const fms::Status started = g_runtime.start();
  if (!fms::is_ok(started)) {
    std::fprintf(stderr, "start failed: %s\n", fms::to_string(started));
    return 1;
  }

  // ---- run phase: no allocation past this point ---------------------------
  for (;;) {
    const fms::Status status = g_runtime.service();
    if (status == fms::Status::EndOfInput) {
      break;
    }
    if (!fms::is_ok(status)) {
      std::fprintf(stderr, "port error: %s (%s)\n", fms::to_string(status), port.last_error());
      break;
    }
  }

  g_runtime.stop();
  std::fprintf(stderr, "final state '%s': %u transitions, %u rejected, %u inputs (%u unknown)\n",
               g_machine.current_name(), g_machine.transition_count(),
               g_machine.rejection_count(), g_runtime.inputs_received(),
               g_runtime.inputs_unrouted());
  return 0;
}
