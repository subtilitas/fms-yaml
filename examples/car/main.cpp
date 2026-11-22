// SPDX-License-Identifier: MIT
//
// Car state machine on the console.
//
//   ./car_console [setup.yaml] [machine.yaml] [--quiet] [--check] [--lint]
//                 [--export mermaid|dot] [--version]
//
// The YAML is read here, when the program runs, and nowhere else: the build does
// not copy it, parse it or depend on it.  So the paths are yours to choose -
// they default to car.setup.yaml and car.machine.yaml in the working directory:
//
//   cd examples/car && ../../build/car_console
//   ./build/car_console examples/car/car.setup.yaml examples/car/car.machine.yaml
//
// --check loads both files, reports what they describe, runs the linter over
// the result and exits, which is how you validate a configuration without
// building anything.  --lint is the same without the description, for a script
// that only wants the exit code.  Both exit 1 when the linter found an error.
//
// --export writes the machine as a diagram - mermaid for a Markdown file,
// dot for graphviz - so the picture is generated from the configuration rather
// than drawn beside it and left to rot.
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
#include <cstddef>
#include <iomanip>
#include <iostream>

#include <etl/char_traits.h>

#include "fms/abi.hpp"
#include "fms/port/console_port.hpp"
#include "fms/runtime.hpp"
#include "fms/version.hpp"
#include "fms/yaml_loader.hpp"

#if defined(FMS_WITH_INSPECT)
#include "fms/inspect/diagram.hpp"
#include "fms/inspect/lint.hpp"
#endif

namespace {

/// The longest command-line argument read as an option.  Every option and
/// format name is far shorter; a longer argument is clipped to this length,
/// and a view that long equals none of them.
constexpr std::size_t kMaxOptionLength = 64;

fms::StringView option(const char* argument) {
  return fms::StringView(argument, etl::strlen(argument, kMaxOptionLength));
}

void trace(void* user, const fms::TransitionEvent& event) {
  const auto* model = static_cast<const fms::Model*>(user);

  if (event.accepted) {
    std::cerr << "  [" << model->state_name(event.from) << " --"
              << model->trigger_name(event.trigger) << "--> " << model->state_name(event.to)
              << "]\n";
  }
}

/// Reports a failed load with the file it came from, so a two-file setup does
/// not leave you guessing which one is wrong.
void report(const char* path, fms::Status status, const fms::config::Diagnostics& diagnostics) {
  std::cerr << path << ": " << fms::to_string(status);
  if (diagnostics.line > 0) {
    std::cerr << " at line " << diagnostics.line;
  }
  std::cerr << ": " << diagnostics.message.c_str() << '\n';
  if (status == fms::Status::FileNotFound) {
    std::cerr << "hint: pass the paths, e.g. examples/car/car.setup.yaml "
                 "examples/car/car.machine.yaml\n";
  }
}

/// --check: say what the two files describe, then stop.
void describe(const fms::Setup& setup, const fms::Model& model, const fms::StateMachine& machine) {
  std::cout << "setup   : instance '" << setup.name().c_str() << "', starts in '"
            << setup.initial_name().c_str() << "'\n";
  std::cout << "machine : '" << model.name().c_str() << "', " << model.state_count()
            << " states, " << model.trigger_count() << " triggers, " << model.condition_count()
            << " guard conditions\n";

  for (const auto& entry : model.states()) {
    const fms::StateId id = entry.first;
    std::cout << "  " << std::left << std::setw(13) << model.state_name(id) << std::right << ' '
              << ((id == machine.initial()) ? "* " : "  ") << entry.second.transitions.size()
              << " trigger(s)";
    for (const auto& transition : entry.second.transitions) {
      std::cout << ' ' << model.trigger_name(transition.first);
      if (transition.second.size() > 1) {
        std::cout << '(' << transition.second.size() << ')';
      }
    }
    std::cout << '\n';
  }
}

#if defined(FMS_WITH_INSPECT)

/// Runs the linter over the loaded machine and prints what it found.  Returns
/// false when any finding was an error, which is the program's exit code.
bool report_lint(const fms::Model& model, fms::StateId initial) {
  fms::lint::Report report;
  const fms::Status status = fms::lint::analyse(model, initial, report);

  if (report.empty()) {
    std::cout << "lint    : clean\n";
    return true;
  }

  std::cout << "lint    : " << report.size() << " finding(s)\n";

  fms::Message text;
  for (const fms::lint::Finding& finding : report) {
    fms::lint::describe(model, finding, text);
    std::cout << "  " << std::left << std::setw(7)
              << fms::lint::to_string(fms::lint::severity_of(finding.check)) << ' '
              << std::setw(23) << fms::lint::to_string(finding.check) << std::right << ' '
              << text.c_str() << '\n';
  }
  if (status == fms::Status::CapacityExceeded) {
    std::cout << "  ... report full; raise FMS_MAX_FINDINGS to see the rest\n";
  }
  return !fms::lint::has_errors(report);
}

/// The diagram arrives in fragments, in order; there is nothing to assemble.
void write_fragment(void* /*user*/, fms::StringView text) {
  std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
}

#endif  // defined(FMS_WITH_INSPECT)

// The whole system, statically allocated.
fms::Setup        g_setup;
fms::Model        g_model;
fms::StateMachine g_machine;
fms::Runtime      g_runtime;

}  // namespace

int main(int argc, char** argv) {
  // A trace line goes to stderr the moment it is written, as stdio's stderr
  // does, rather than first flushing what std::cout still holds.
  std::cerr.tie(nullptr);

  const char* setup_path   = "car.setup.yaml";
  const char* machine_path = "car.machine.yaml";
  bool        quiet        = false;
  bool        check_only   = false;
  bool        lint_only    = false;
  int         positional   = 0;

#if defined(FMS_WITH_INSPECT)
  bool                 export_diagram = false;
  fms::diagram::Format format         = fms::diagram::Format::Mermaid;
#endif

  for (int i = 1; i < argc; ++i) {
    const fms::StringView argument = option(argv[i]);
    if (argument == fms::StringView("--version")) {
      // The capacities go with the version because they are half of what
      // decides whether two builds are the same build.
      std::cout << "fms-yaml " << fms::version() << " (capacities " << fms::abi::tag() << ")\n";
      return 0;
    }
    if (argument == fms::StringView("--quiet")) {
      quiet = true;
    } else if (argument == fms::StringView("--check")) {
      check_only = true;
    } else if (argument == fms::StringView("--lint")) {
#if defined(FMS_WITH_INSPECT)
      lint_only = true;
#else
      std::cerr << "built without fms_inspect: --lint is unavailable\n";
      return 2;
#endif
    } else if (argument == fms::StringView("--export")) {
#if defined(FMS_WITH_INSPECT)
      const char* name = (i + 1 < argc) ? argv[++i] : "";
      if (!fms::diagram::parse_format(option(name), format)) {
        std::cerr << "usage: --export mermaid|dot\n";
        return 2;
      }
      export_diagram = true;
#else
      std::cerr << "built without fms_inspect: --export is unavailable\n";
      return 2;
#endif
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
    std::cerr << setup_path << " does not fit " << machine_path << ": " << fms::to_string(bound)
              << " ('" << g_setup.initial_name().c_str() << "')\n";
    return 1;
  }

#if defined(FMS_WITH_INSPECT)
  // A diagram is about the machine alone, but the entry arrow comes from the
  // setup - which is why this needs both files, like everything else here.
  if (export_diagram) {
    // Best effort: the diagram goes to stdout, and a writer that refuses it has
    // already made that visible.  Said with a cast rather than left to look
    // like an oversight.
    (void)fms::diagram::render(g_model, g_machine.initial(), format, &write_fragment, nullptr);
    return 0;
  }
#endif

  if (check_only || lint_only) {
    if (check_only) {
      describe(g_setup, g_model, g_machine);
    }
#if defined(FMS_WITH_INSPECT)
    if (!report_lint(g_model, g_machine.initial())) {
      return 1;
    }
#endif
    if (check_only) {
      std::cout << "ok\n";
    }
    return 0;
  }

  fms::port::ConsolePort port(/*prompt=*/!quiet);

  if (!fms::is_ok(g_runtime.init(g_machine, port))) {
    std::cerr << "runtime init failed\n";
    return 1;
  }

  if (!quiet) {
    g_runtime.set_trace(&trace, &g_model);
    std::cout << g_setup.name().c_str() << " running '" << g_model.name().c_str()
              << "': " << g_model.state_count() << " states, " << g_model.trigger_count()
              << " triggers - type 'help' or 'quit'\n";
  }

  const fms::Status started = g_runtime.start();
  if (!fms::is_ok(started)) {
    std::cerr << "start failed: " << fms::to_string(started) << '\n';
    return 1;
  }

  // ---- run phase: no allocation past this point ---------------------------
  for (;;) {
    const fms::Status status = g_runtime.service();
    if (status == fms::Status::EndOfInput) {
      break;
    }
    if (!fms::is_ok(status)) {
      std::cerr << "port error: " << fms::to_string(status) << " (" << port.last_error()
                << ")\n";
      break;
    }
  }

  // Shutting down on the way out; there is nothing left to report a failure to.
  (void)g_runtime.stop();
  std::cerr << "final state '" << g_machine.current_name() << "': "
            << g_machine.transition_count() << " transitions, " << g_machine.rejection_count()
            << " rejected, " << g_runtime.inputs_received() << " inputs ("
            << g_runtime.inputs_unrouted() << " unknown)\n";
  return 0;
}
