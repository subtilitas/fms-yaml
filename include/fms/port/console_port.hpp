// SPDX-License-Identifier: MIT
//
// An IPort over std::cin and std::cout.  Type a trigger name, get the new state
// back.  Works just as well with a pipe:
//
//     printf 'ignition_on\nselftest_passed\n' | ./car_console car.yaml
//
// Lives in its own target (fms_console) because it pulls in <iostream>, which a
// firmware build has no use for.  The core does not depend on it.
//
// Reading is line based and blocking: `timeout_ms` is ignored, because there is
// no portable way to wait on stdin with a timeout.  That is fine for a console
// driven machine, which has nothing else to do.
#ifndef FMS_PORT_CONSOLE_PORT_HPP
#define FMS_PORT_CONSOLE_PORT_HPP

#include "fms/port.hpp"

namespace fms::port {

class ConsolePort final : public IPort {
 public:
  /// `prompt` prints "> " before each read, which is what you want
  /// interactively and not what you want in a pipe.
  explicit ConsolePort(bool prompt = true) noexcept : prompt_(prompt) {}

  ConsolePort(const ConsolePort&) = delete;
  ConsolePort& operator=(const ConsolePort&) = delete;

  Status open() noexcept override;
  Status close() noexcept override;

  /// Announced channels are only remembered so that "help" can list them.
  Status listen(StringView channel) noexcept override;

  /// Reads one line.  Blank lines are skipped, "help" lists the channels,
  /// "quit" ends the session.  Blocks; `timeout_ms` is ignored.
  Status receive(StringView& channel, std::uint32_t timeout_ms) noexcept override;

  Status publish_state(StringView state) noexcept override;
  Status publish_error(StringView message) noexcept override;

  const char* last_error() const noexcept override { return "none"; }

 private:
  void print_help() noexcept;

  static constexpr std::size_t kMaxChannels = limits::kMaxTriggers;

  bool prompt_ = true;

  /// The line buffer the returned view points into; owned by the port and valid
  /// until the next receive(), exactly as IPort requires.
  char        buffer_[limits::kMaxChannelLength + 1] = {};
  std::size_t length_ = 0;

  Channel     channels_[kMaxChannels]{};
  std::size_t channel_count_ = 0;
};

}  // namespace fms::port

#endif  // FMS_PORT_CONSOLE_PORT_HPP
