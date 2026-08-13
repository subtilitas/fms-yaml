// SPDX-License-Identifier: MIT
//
// Car state machine on the console.
//
//   ./car_console [car.yaml] [--quiet]
//
// Type a trigger name and press enter; "help" lists them, "quit" ends the
// session.  It pipes just as well:
//
//   printf 'ignition_on\nself_test_passed\nthrottle_pressed\n' | ./car_console car.yaml --quiet
//
// There is nothing to register: the machine is entirely described by the YAML
// file, and the only application code is the trace hook below.
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

// The whole system, statically allocated.
fms::Model        g_model;
fms::StateMachine g_machine;
fms::Runtime      g_runtime;

}  // namespace

int main(int argc, char** argv) {
  const char* path  = "car.yaml";
  bool        quiet = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quiet") == 0) {
      quiet = true;
    } else {
      path = argv[i];
    }
  }

  // ---- setup phase: the only place that may allocate ----------------------
  fms::config::Diagnostics diagnostics;
  const fms::Status loaded = fms::config::load_file(path, g_model, diagnostics);
  if (!fms::is_ok(loaded)) {
    std::fprintf(stderr, "config error (%s) at line %d: %s\n", fms::to_string(loaded),
                 diagnostics.line, diagnostics.message.c_str());
    return 1;
  }

  fms::port::ConsolePort port(/*prompt=*/!quiet);

  if (!fms::is_ok(g_machine.init(g_model))) {
    std::fputs("state machine init failed\n", stderr);
    return 1;
  }
  if (!fms::is_ok(g_runtime.init(g_model, g_machine, port))) {
    std::fputs("runtime init failed\n", stderr);
    return 1;
  }
  if (!quiet) {
    g_runtime.set_trace(&trace, &g_model);
    std::printf("%s: %zu states, %zu triggers - type 'help' or 'quit'\n", g_model.name().c_str(),
                g_model.state_count(), g_model.trigger_count());
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
