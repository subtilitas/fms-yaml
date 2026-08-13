// SPDX-License-Identifier: MIT
//
// Car state machine on the console.
//
//   ./car_console [car.setup.yaml] [car.machine.yaml] [--quiet]
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
  int         positional   = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quiet") == 0) {
      quiet = true;
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

  fms::port::ConsolePort port(/*prompt=*/!quiet);

  // init() is where the two files have to agree: the initial state named by the
  // setup must exist in the machine.
  const fms::Status bound = g_machine.init(g_model, g_setup);
  if (!fms::is_ok(bound)) {
    std::fprintf(stderr, "%s does not fit %s: %s ('%s')\n", setup_path, machine_path,
                 fms::to_string(bound), g_setup.initial_name().c_str());
    return 1;
  }
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
