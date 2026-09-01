// SPDX-License-Identifier: MIT
//
// Channel -> trigger -> state change -> publication, and the error feedback for
// a trigger the current state does not accept.  Uses MemoryPort, so no I/O is
// involved.
#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "fms/port/memory_port.hpp"
#include "fms/runtime.hpp"
#include "fms/yaml_loader.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

int g_traced   = 0;
int g_rejected = 0;

void trace(void*, const fms::TransitionEvent& event) {
  ++g_traced;
  if (!event.accepted) {
    ++g_rejected;
  }
}

constexpr const char* kSetup = R"(
fsm:
  name: car-under-test
  initial: standing
io:
  state_channel: "car/state"
  error_channel: "car/error"
  endpoint: "loopback"
  identity: "unit-test"
)";

constexpr const char* kMachine = R"(
fsm:
  name: car
triggers:
  - {name: throttle_pressed}
  - {name: brake_pressed, channel: "car/brakes/pressed"}
  - {name: vehicle_stopped}
states:
  - name: standing
    transitions:
      throttle_pressed: accelerating
  - name: accelerating
    transitions:
      brake_pressed: braking
  - name: braking
    transitions:
      vehicle_stopped: standing
)";

struct Harness {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::Runtime             runtime;
  fms::port::MemoryPort<>  port;
  fms::config::Diagnostics diagnostics;

  Harness() {
    g_traced   = 0;
    g_rejected = 0;

    REQUIRE(fms::config::load_setup_string(kSetup, setup, diagnostics) == fms::Status::Ok);
    const fms::Status loaded = fms::config::load_machine_string(kMachine, model, diagnostics);
    INFO("diagnostic: ", diagnostics.message.c_str());
    REQUIRE(loaded == fms::Status::Ok);

    REQUIRE(machine.init(model, setup) == fms::Status::Ok);
    REQUIRE(runtime.init(machine, port) == fms::Status::Ok);
    runtime.set_trace(&trace, nullptr);
    REQUIRE(runtime.start() == fms::Status::Ok);
  }

  void deliver(const char* channel) {
    REQUIRE(port.inject(sv(channel)) == fms::Status::Ok);
    REQUIRE(runtime.service(0) == fms::Status::Ok);
  }
};

}  // namespace

TEST_CASE("start configures and opens the port, announces every channel and publishes the initial state") {
  Harness h;
  CHECK(h.port.is_open());
  CHECK(h.port.listening().size() == 3);
  CHECK(h.port.last_state() == sv("standing"));
}

TEST_CASE("the io block of the setup reaches the port") {
  Harness h;
  // configure() ran before open(), with the values from the setup file.
  CHECK(h.port.configured());
  CHECK(sv(h.port.io().endpoint.c_str()) == sv("loopback"));
  CHECK(sv(h.port.io().identity.c_str()) == sv("unit-test"));
  CHECK(sv(h.port.io().state_channel.c_str()) == sv("car/state"));
  CHECK(h.port.configured_before_open());
}

TEST_CASE("a runtime cannot be bound to an uninitialised machine") {
  Harness            h;
  fms::StateMachine  fresh;
  fms::Runtime       runtime;
  fms::port::MemoryPort<> port;
  CHECK(runtime.init(fresh, port) == fms::Status::NotInitialised);
}

TEST_CASE("a trigger without an explicit channel listens on its own name") {
  Harness h;
  CHECK(h.model.find_trigger_for_channel(sv("throttle_pressed")) ==
        h.model.find_trigger(sv("throttle_pressed")));
  // ...and one with an explicit channel listens only there.
  CHECK(h.model.find_trigger_for_channel(sv("car/brakes/pressed")) ==
        h.model.find_trigger(sv("brake_pressed")));
  CHECK(h.model.find_trigger_for_channel(sv("brake_pressed")) == fms::kNoTrigger);
}

TEST_CASE("input on a trigger channel changes the state and publishes it") {
  Harness h;

  h.deliver("throttle_pressed");
  CHECK(std::strcmp(h.machine.current_name(), "accelerating") == 0);
  CHECK(h.port.last_state() == sv("accelerating"));

  h.deliver("car/brakes/pressed");
  CHECK(std::strcmp(h.machine.current_name(), "braking") == 0);
  CHECK(h.port.last_state() == sv("braking"));

  h.deliver("vehicle_stopped");
  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
  CHECK(h.machine.transition_count() == 3);
  CHECK(h.port.errors().empty());
}

TEST_CASE("a trigger the current state does not accept is reported as an error") {
  Harness h;
  h.port.clear_history();

  h.deliver("car/brakes/pressed");  // the car is standing

  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
  CHECK(h.machine.rejection_count() == 1);
  CHECK(h.port.last_error_message() == sv("rejected: brake_pressed in state standing"));
  CHECK(h.port.states().empty());  // the state did not change, so nothing was published
}

TEST_CASE("input on an unknown channel is counted and reported") {
  Harness h;
  h.deliver("radio_volume");

  CHECK(h.runtime.inputs_received() == 1);
  CHECK(h.runtime.inputs_unrouted() == 1);
  CHECK(h.machine.rejection_count() == 0);
  CHECK(h.port.last_error_message() == sv("unknown channel: radio_volume"));
  CHECK(std::strcmp(h.machine.current_name(), "standing") == 0);
}

TEST_CASE("an idle port is not an error") {
  Harness h;
  CHECK(h.runtime.service(0) == fms::Status::Ok);  // nothing queued -> Timeout inside
  CHECK(h.runtime.inputs_received() == 0);
}

TEST_CASE("an exhausted port ends the loop") {
  Harness h;
  h.port.set_end_of_input();
  CHECK(h.runtime.service(0) == fms::Status::EndOfInput);
}

TEST_CASE("the trace hook sees accepted and rejected triggers alike") {
  Harness h;
  h.deliver("throttle_pressed");  // accepted
  h.deliver("vehicle_stopped");   // rejected while accelerating

  CHECK(g_traced == 2);
  CHECK(g_rejected == 1);
}

TEST_CASE("application code can raise a trigger by name") {
  Harness h;
  CHECK(h.runtime.fire_by_name(sv("throttle_pressed")) == fms::Status::Ok);
  CHECK(std::strcmp(h.machine.current_name(), "accelerating") == 0);

  CHECK(h.runtime.fire_by_name(sv("vehicle_stopped")) == fms::Status::NoTransition);
  CHECK(h.runtime.fire_by_name(sv("horn")) == fms::Status::UnknownTrigger);
}

TEST_CASE("stop closes the port") {
  Harness h;
  REQUIRE(h.runtime.stop() == fms::Status::Ok);
  CHECK_FALSE(h.port.is_open());
}

namespace {

/// Overrides only the three methods IPort leaves pure.  configure, open,
/// listen, close and last_error are the base class's defaults, which the
/// documentation claims are usable as they stand - this is what checks that.
class DefaultsPort final : public fms::IPort {
 public:
  fms::Status receive(fms::Input& input, std::uint32_t /*timeout_ms*/) noexcept override {
    if (delivered_) {
      return fms::Status::EndOfInput;
    }
    delivered_        = true;
    input.channel     = sv("throttle_pressed");
    input.arguments   = fms::StringView{};
    return fms::Status::Ok;
  }

  fms::Status publish_state(fms::StringView state) noexcept override {
    fms::assign_checked(last_state_, state);
    ++states_;
    return fms::Status::Ok;
  }

  fms::Status publish_error(fms::StringView /*message*/) noexcept override {
    ++errors_;
    return fms::Status::Ok;
  }

  const fms::Message& last_state() const noexcept { return last_state_; }
  int                 states() const noexcept { return states_; }
  int                 errors() const noexcept { return errors_; }

 private:
  bool         delivered_ = false;
  int          states_    = 0;
  int          errors_    = 0;
  fms::Message last_state_{};
};

}  // namespace

TEST_CASE("a port that implements only the three required methods still runs") {
  fms::Setup               setup;
  fms::Model               model;
  fms::config::Diagnostics diagnostics;
  REQUIRE(fms::config::load_setup_string(kSetup, setup, diagnostics) == fms::Status::Ok);
  REQUIRE(fms::config::load_machine_string(kMachine, model, diagnostics) == fms::Status::Ok);

  fms::StateMachine machine;
  REQUIRE(machine.init(model, setup) == fms::Status::Ok);

  DefaultsPort port;
  fms::Runtime runtime;
  REQUIRE(runtime.init(machine, port) == fms::Status::Ok);

  // start() calls configure(), open() and listen() once per trigger.  All three
  // are inherited, so this returning Ok is the whole point of the test.
  REQUIRE(runtime.start() == fms::Status::Ok);
  CHECK(port.states() == 1);
  CHECK(std::strcmp(port.last_state().c_str(), "standing") == 0);

  CHECK(runtime.service(0) == fms::Status::Ok);
  CHECK(std::strcmp(port.last_state().c_str(), "accelerating") == 0);
  CHECK(port.errors() == 0);

  CHECK(runtime.service(0) == fms::Status::EndOfInput);
  CHECK(runtime.stop() == fms::Status::Ok);          // inherited close()
  CHECK(std::strcmp(port.last_error(), "none") == 0);  // inherited last_error()
}

TEST_CASE("MemoryPort refuses to work before it is opened") {
  fms::port::MemoryPort<> port;

  fms::Input input;
  CHECK(port.receive(input, 0) == fms::Status::NotOpen);
  CHECK(port.listen(sv("anything")) == fms::Status::NotOpen);
  CHECK(port.publish_state(sv("standing")) == fms::Status::NotOpen);
  CHECK(port.publish_error(sv("nothing happened")) == fms::Status::NotOpen);
}

TEST_CASE("MemoryPort refuses what it cannot store") {
  // The default instantiation on purpose: a MemoryPort<2> would be a second
  // template instantiation, and the half of it no test calls would then be
  // counted as uncovered code that does not otherwise exist.
  fms::port::MemoryPort<> port;
  REQUIRE(port.open() == fms::Status::Ok);

  const std::string too_long(fms::limits::kMaxChannelLength + 1, 'c');
  CHECK(port.listen(fms::StringView(too_long.data(), too_long.size())) ==
        fms::Status::ChannelTooLong);
  CHECK(port.inject(fms::StringView(too_long.data(), too_long.size())) ==
        fms::Status::ChannelTooLong);

  for (int i = 0; i < 16; ++i) {   // the default QueueDepth
    const std::string name = "queued_" + std::to_string(i);
    REQUIRE(port.inject(fms::StringView(name.data(), name.size())) == fms::Status::Ok);
  }
  CHECK(port.inject(sv("one_too_many")) == fms::Status::CapacityExceeded);
}

namespace {

/// A port that fails whichever of the three start-up calls it is told to.
/// start() reports the port's own Status rather than a generic failure, which
/// is what tells a caller whether the endpoint was wrong or the broker was
/// simply down.  Built on IPort directly - MemoryPort is final, and this needs
/// to answer differently anyway.
class FailingPort final : public fms::IPort {
 public:
  enum class Fail : std::uint8_t { Configure, Open, Listen };

  explicit FailingPort(Fail fail) noexcept : fail_(fail) {}

  fms::Status configure(const fms::IoConfig& /*io*/) noexcept override {
    if (fail_ == Fail::Configure) {
      return fms::Status::InvalidArgument;
    }
    configured_ = true;
    return fms::Status::Ok;
  }

  fms::Status open() noexcept override {
    if (fail_ == Fail::Open) {
      return fms::Status::PortError;
    }
    opened_ = true;
    return fms::Status::Ok;
  }

  fms::Status listen(fms::StringView /*channel*/) noexcept override {
    return (fail_ == Fail::Listen) ? fms::Status::CapacityExceeded : fms::Status::Ok;
  }

  fms::Status receive(fms::Input& /*input*/, std::uint32_t /*timeout_ms*/) noexcept override {
    return fms::Status::EndOfInput;
  }

  fms::Status publish_state(fms::StringView /*state*/) noexcept override {
    return fms::Status::Ok;
  }

  fms::Status publish_error(fms::StringView /*message*/) noexcept override {
    return fms::Status::Ok;
  }

  bool configured() const noexcept { return configured_; }
  bool opened() const noexcept { return opened_; }

 private:
  Fail fail_;
  bool configured_ = false;
  bool opened_     = false;
};

/// setup + machine + a bound StateMachine, ready for a Runtime.
struct Bound {
  fms::Setup               setup;
  fms::Model               model;
  fms::StateMachine        machine;
  fms::config::Diagnostics diagnostics;

  Bound() {
    REQUIRE(fms::config::load_setup_string(kSetup, setup, diagnostics) == fms::Status::Ok);
    REQUIRE(fms::config::load_machine_string(kMachine, model, diagnostics) == fms::Status::Ok);
    REQUIRE(machine.init(model, setup) == fms::Status::Ok);
  }
};

}  // namespace

TEST_CASE("a port that cannot start stops the runtime, and says which call failed") {
  SUBCASE("configure") {
    Bound        bound;
    FailingPort  port(FailingPort::Fail::Configure);
    fms::Runtime runtime;
    REQUIRE(runtime.init(bound.machine, port) == fms::Status::Ok);
    CHECK(runtime.start() == fms::Status::InvalidArgument);
    CHECK(!port.opened());           // open() was never reached
  }
  SUBCASE("open") {
    Bound        bound;
    FailingPort  port(FailingPort::Fail::Open);
    fms::Runtime runtime;
    REQUIRE(runtime.init(bound.machine, port) == fms::Status::Ok);
    CHECK(runtime.start() == fms::Status::PortError);
    CHECK(port.configured());        // ...but configure() was
  }
  SUBCASE("listen") {
    Bound        bound;
    FailingPort  port(FailingPort::Fail::Listen);
    fms::Runtime runtime;
    REQUIRE(runtime.init(bound.machine, port) == fms::Status::Ok);
    CHECK(runtime.start() == fms::Status::CapacityExceeded);
  }
}

TEST_CASE("the runtime refuses to work out of order") {
  Bound                   bound;
  fms::port::MemoryPort<> port;
  fms::Runtime            runtime;

  SUBCASE("service before start") {
    CHECK(runtime.service(0) == fms::Status::NotInitialised);
  }
  SUBCASE("fire_by_name before start") {
    CHECK(runtime.fire_by_name(sv("throttle_pressed")) == fms::Status::NotInitialised);
  }
  SUBCASE("stop before there is a port") {
    CHECK(runtime.stop() == fms::Status::NotInitialised);
  }
  SUBCASE("start before init") {
    CHECK(runtime.start() == fms::Status::NotInitialised);
  }
  SUBCASE("started twice") {
    REQUIRE(runtime.init(bound.machine, port) == fms::Status::Ok);
    REQUIRE(runtime.start() == fms::Status::Ok);
    CHECK(runtime.start() == fms::Status::AlreadyInitialised);
    CHECK(runtime.init(bound.machine, port) == fms::Status::AlreadyInitialised);
  }
}

TEST_CASE("fire_by_name reports a name the machine does not have") {
  Bound                   bound;
  fms::port::MemoryPort<> port;
  fms::Runtime            runtime;
  REQUIRE(runtime.init(bound.machine, port) == fms::Status::Ok);
  REQUIRE(runtime.start() == fms::Status::Ok);

  CHECK(runtime.fire_by_name(sv("no_such_trigger")) == fms::Status::UnknownTrigger);
  // Malformed arguments are the caller's error here, not a published rejection.
  CHECK(runtime.fire_by_name(sv("throttle_pressed"), sv("pedal")) == fms::Status::ArgumentError);
  CHECK(std::strcmp(bound.machine.current_name(), "standing") == 0);
}
