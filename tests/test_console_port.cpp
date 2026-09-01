// SPDX-License-Identifier: MIT
//
// ConsolePort's line handling: the blank line, the trailing CR from a Windows
// editor, the line too long for the fixed buffer, and the two words it answers
// itself.  None of that is exercised by the scripted session in
// tests/car_session.txt, which drives the built binary through a pipe and so
// only sees well-formed input.
//
// The port reads std::cin and writes std::cout and std::cerr directly, which is
// the whole reason it lives in its own target.  Swapping the three stream
// buffers for string buffers is enough to drive it in process; RAII puts them
// back, including when a REQUIRE throws out of the middle of a test.
#include <doctest/doctest.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "fms/port/console_port.hpp"

namespace {

fms::StringView sv(const char* text) { return fms::StringView(text, std::strlen(text)); }

std::string text_of(fms::StringView view) { return std::string(view.data(), view.size()); }

/// Redirects cin, cout and cerr for the lifetime of the object.
class Streams {
 public:
  explicit Streams(const std::string& input)
      : synced_(desync()),
        in_(input),
        old_cin_(std::cin.rdbuf(in_.rdbuf())),
        old_cout_(std::cout.rdbuf(out_.rdbuf())),
        old_cerr_(std::cerr.rdbuf(err_.rdbuf())) {
    // A previous test may have left eofbit set; the flags belong to the stream,
    // not to the buffer we just swapped in.
    std::cin.clear();
  }

  ~Streams() {
    std::cin.rdbuf(old_cin_);
    std::cout.rdbuf(old_cout_);
    std::cerr.rdbuf(old_cerr_);
    std::cin.clear();
  }

  Streams(const Streams&) = delete;
  Streams& operator=(const Streams&) = delete;

  std::string out() const { return out_.str(); }
  std::string err() const { return err_.str(); }

 private:
  /// ConsolePort::open() calls sync_with_stdio(false) as well.  libstdc++ acts
  /// only on the first true->false transition, and acting means replacing the
  /// buffers of cin, cout and cerr - so it has to happen before ours go in, or
  /// it would swap them straight back out and the port would block on the real
  /// stdin.  Declared first so it is initialised first.
  static bool desync() noexcept {
    std::ios_base::sync_with_stdio(false);
    return true;
  }

  [[maybe_unused]] bool synced_;  // ordering device only; never read
  std::istringstream in_;
  std::ostringstream out_;
  std::ostringstream err_;
  std::streambuf*    old_cin_;
  std::streambuf*    old_cout_;
  std::streambuf*    old_cerr_;
};

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("a blank line asks again rather than arriving as an empty channel") {
  Streams              streams("\n   \n\t\nthrottle_pressed pedal=60\n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  REQUIRE(port.receive(input, 0) == fms::Status::Ok);
  CHECK(text_of(input.channel) == "throttle_pressed");
  CHECK(text_of(input.arguments) == "pedal=60");
}

TEST_CASE("quit and exit both end the session") {
  SUBCASE("quit") {
    Streams              streams("quit\n");
    fms::port::ConsolePort port(/*prompt=*/false);
    REQUIRE(port.open() == fms::Status::Ok);
    fms::Input input;
    CHECK(port.receive(input, 0) == fms::Status::EndOfInput);
  }
  SUBCASE("exit") {
    Streams              streams("exit\n");
    fms::port::ConsolePort port(/*prompt=*/false);
    REQUIRE(port.open() == fms::Status::Ok);
    fms::Input input;
    CHECK(port.receive(input, 0) == fms::Status::EndOfInput);
  }
}

TEST_CASE("an exhausted stream is end of input, not an error") {
  Streams              streams("");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  CHECK(port.receive(input, 0) == fms::Status::EndOfInput);
}

TEST_CASE("help answers from the port itself and the session continues") {
  Streams              streams("help\nthrottle\n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);
  REQUIRE(port.listen(sv("throttle")) == fms::Status::Ok);
  REQUIRE(port.listen(sv("brake")) == fms::Status::Ok);

  fms::Input input;
  REQUIRE(port.receive(input, 0) == fms::Status::Ok);
  CHECK(text_of(input.channel) == "throttle");

  const std::string out = streams.out();
  CHECK(contains(out, "channels (2)"));
  CHECK(contains(out, "throttle"));
  CHECK(contains(out, "brake"));
  CHECK(contains(out, "quit"));
}

TEST_CASE("? is help as well") {
  Streams              streams("?\nquit\n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  CHECK(port.receive(input, 0) == fms::Status::EndOfInput);
  CHECK(contains(streams.out(), "channels (0)"));
}

TEST_CASE("a trailing carriage return is trimmed, so a Windows line still routes") {
  Streams              streams("throttle_pressed pedal=60\r\n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  REQUIRE(port.receive(input, 0) == fms::Status::Ok);
  CHECK(text_of(input.channel) == "throttle_pressed");
  // The CR belongs to neither the channel nor the arguments.
  CHECK(text_of(input.arguments) == "pedal=60");
}

TEST_CASE("surrounding whitespace is trimmed and the first word is the channel") {
  Streams              streams("   throttle_pressed    pedal=60 mode=sport   \n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  REQUIRE(port.receive(input, 0) == fms::Status::Ok);
  CHECK(text_of(input.channel) == "throttle_pressed");
  CHECK(text_of(input.arguments) == "pedal=60 mode=sport");
}

TEST_CASE("a channel with no arguments carries an empty argument view") {
  Streams              streams("ignition_on\n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  REQUIRE(port.receive(input, 0) == fms::Status::Ok);
  CHECK(text_of(input.channel) == "ignition_on");
  CHECK(input.arguments.empty());
}

TEST_CASE("a line too long for the buffer is refused, and the next one is read") {
  // The buffer holds a channel plus a message; anything longer cannot be stored
  // without truncating, which would silently change what the sender said.
  const std::string overlong(400, 'x');
  Streams              streams(overlong + "\nignition_on\n");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  fms::Input input;
  REQUIRE(port.receive(input, 0) == fms::Status::Ok);
  CHECK(text_of(input.channel) == "ignition_on");
  CHECK(contains(streams.err(), "input longer than"));
}

TEST_CASE("listen refuses what it cannot store rather than truncating") {
  Streams              streams("");
  fms::port::ConsolePort port(/*prompt=*/false);

  const std::string too_long(fms::limits::kMaxChannelLength + 1, 'c');
  CHECK(port.listen(fms::StringView(too_long.data(), too_long.size())) ==
        fms::Status::ChannelTooLong);
}

TEST_CASE("listen has a ceiling, and says so") {
  Streams              streams("");
  fms::port::ConsolePort port(/*prompt=*/false);

  for (std::size_t i = 0; i < fms::limits::kMaxTriggers; ++i) {
    const std::string name = "channel_" + std::to_string(i);
    REQUIRE(port.listen(fms::StringView(name.data(), name.size())) == fms::Status::Ok);
  }
  CHECK(port.listen(sv("one_too_many")) == fms::Status::CapacityExceeded);
}

TEST_CASE("state goes to stdout and errors to stderr, so a pipe can separate them") {
  Streams              streams("");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);

  CHECK(port.publish_state(sv("standing")) == fms::Status::Ok);
  CHECK(port.publish_error(sv("rejected: brake_released in state power_off")) ==
        fms::Status::Ok);

  CHECK(contains(streams.out(), "state: standing"));
  CHECK(!contains(streams.out(), "rejected"));
  CHECK(contains(streams.err(), "error: rejected: brake_released in state power_off"));
  CHECK(!contains(streams.err(), "state: standing"));
}

TEST_CASE("the prompt is what --quiet turns off") {
  SUBCASE("on") {
    Streams              streams("ignition_on\n");
    fms::port::ConsolePort port(/*prompt=*/true);
    REQUIRE(port.open() == fms::Status::Ok);
    fms::Input input;
    REQUIRE(port.receive(input, 0) == fms::Status::Ok);
    CHECK(contains(streams.out(), "> "));
  }
  SUBCASE("off") {
    Streams              streams("ignition_on\n");
    fms::port::ConsolePort port(/*prompt=*/false);
    REQUIRE(port.open() == fms::Status::Ok);
    fms::Input input;
    REQUIRE(port.receive(input, 0) == fms::Status::Ok);
    CHECK(!contains(streams.out(), "> "));
  }
}

TEST_CASE("close flushes, and a port that has not failed says so") {
  Streams              streams("");
  fms::port::ConsolePort port(/*prompt=*/false);
  REQUIRE(port.open() == fms::Status::Ok);
  CHECK(std::strcmp(port.last_error(), "none") == 0);
  CHECK(port.close() == fms::Status::Ok);
}
