// SPDX-License-Identifier: MIT
//
// Car state machine demo.
//
//   ./car_ecu [car.yaml]
//
// Talks to a broker on tcp://localhost:1883 (override in car.yaml).  Try:
//   mosquitto_sub -t 'car/#' -v
//   mosquitto_pub -t car/ignition/on -n
//   mosquitto_pub -t car/selftest/passed -n
//   mosquitto_pub -t car/engine/throttle_pressed -n
//   mosquitto_pub -t car/brakes/pressed -n        # accepted while accelerating
//   mosquitto_pub -t car/brakes/released -n       # rejected while standing
//
// There is nothing to register: the machine is entirely described by the YAML
// file.  Everything after load_file() runs out of fixed storage.
#include <atomic>
#include <csignal>
#include <cstdio>

#include "fms/mqtt/loopback_transport.hpp"
#include "fms/runtime.hpp"
#include "fms/yaml_loader.hpp"

#if FMS_WITH_PAHO
#include "fms/mqtt/paho_transport.hpp"
#endif

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

void trace(void* user, const fms::TransitionEvent& event) {
  const auto* model = static_cast<const fms::Model*>(user);

  if (event.accepted) {
    std::printf("[car] %-13s --%s--> %s\n", model->state_name(event.from),
                model->trigger_name(event.trigger), model->state_name(event.to));
  } else {
    std::printf("[car] %-13s rejected '%s'\n", model->state_name(event.from),
                model->trigger_name(event.trigger));
  }
  std::fflush(stdout);
}

// The whole system, statically allocated.
fms::Model        g_model;
fms::StateMachine g_machine;
fms::Runtime      g_runtime;

#if FMS_WITH_PAHO
fms::mqtt::PahoTransport g_transport;
#else
fms::mqtt::LoopbackTransport<> g_transport;
#endif

}  // namespace

int main(int argc, char** argv) {
  const char* path = (argc > 1) ? argv[1] : "car.yaml";

  // ---- setup phase: the only place that may allocate ----------------------
  fms::config::Diagnostics diagnostics;
  const fms::Status loaded = fms::config::load_file(path, g_model, diagnostics);
  if (!fms::is_ok(loaded)) {
    std::fprintf(stderr, "config error (%s) at line %d: %s\n", fms::to_string(loaded),
                 diagnostics.line, diagnostics.message.c_str());
    return 1;
  }
  std::printf("loaded '%s': %zu states, %zu triggers\n", g_model.name().c_str(),
              g_model.state_count(), g_model.trigger_count());

#if FMS_WITH_PAHO
  const fms::Status opened = g_transport.open(g_model.mqtt());
  if (!fms::is_ok(opened)) {
    std::fprintf(stderr, "mqtt open failed: %s (%s)\n", fms::to_string(opened),
                 g_transport.last_error());
    return 1;
  }
#else
  std::puts("built without paho - running against the loopback transport");
#endif

  if (!fms::is_ok(g_machine.init(g_model))) {
    std::fputs("state machine init failed\n", stderr);
    return 1;
  }
  if (!fms::is_ok(g_runtime.init(g_model, g_machine, g_transport))) {
    std::fputs("runtime init failed\n", stderr);
    return 1;
  }
  g_runtime.set_trace(&trace, &g_model);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  const fms::Status started = g_runtime.start();
  if (!fms::is_ok(started)) {
    std::fprintf(stderr, "start failed: %s (%s)\n", fms::to_string(started),
                 g_transport.last_error());
    return 1;
  }

  // ---- run phase: no allocation past this point ---------------------------
  std::printf("running as '%s' in state '%s' - ctrl-c to stop\n",
              g_model.mqtt().client_id.c_str(), g_machine.current_name());
  while (g_running.load()) {
    const fms::Status status = g_runtime.service(100);
    if (!fms::is_ok(status)) {
      std::fprintf(stderr, "service error: %s (%s)\n", fms::to_string(status),
                   g_transport.last_error());
      break;
    }
  }

  g_runtime.stop();
  std::printf("stopped in state '%s': %u transitions, %u rejected, %u messages (%u unrouted)\n",
              g_machine.current_name(), g_machine.transition_count(),
              g_machine.rejection_count(), g_runtime.messages_received(),
              g_runtime.messages_unrouted());
  return 0;
}
