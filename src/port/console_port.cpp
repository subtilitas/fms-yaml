// SPDX-License-Identifier: MIT
#include "fms/port/console_port.hpp"

#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>

namespace fms::port {
namespace {

bool equals(const char* text, std::size_t length, const char* literal) noexcept {
  return length == std::strlen(literal) && std::strncmp(text, literal, length) == 0;
}

}  // namespace

Status ConsolePort::open() noexcept {
  // Untie cin from cout so a prompt is flushed before the read blocks, and skip
  // the C stdio synchronisation we do not need.
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(&std::cout);
  return Status::Ok;
}

Status ConsolePort::close() noexcept {
  std::cout.flush();
  return Status::Ok;
}

Status ConsolePort::listen(StringView channel) noexcept {
  if (channel_count_ >= kMaxChannels) {
    return Status::CapacityExceeded;
  }
  if (!assign_checked(channels_[channel_count_], channel)) {
    return Status::ChannelTooLong;
  }
  ++channel_count_;
  return Status::Ok;
}

Status ConsolePort::receive(Input& input, std::uint32_t /*timeout_ms*/) noexcept {
  for (;;) {
    if (prompt_) {
      std::cout << "> ";
    }

    // getline into a fixed buffer: no std::string, no allocation.
    std::cin.getline(buffer_, static_cast<std::streamsize>(sizeof(buffer_)));

    if (std::cin.eof() && std::cin.gcount() == 0) {
      if (prompt_) {
        std::cout << '\n';
      }
      return Status::EndOfInput;
    }
    if (std::cin.fail() && !std::cin.eof()) {
      // The line was longer than the buffer: drop the rest and complain.
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cerr << "error: input longer than " << (sizeof(buffer_) - 1) << " characters\n";
      continue;
    }

    length_ = std::strlen(buffer_);

    // Trim trailing whitespace and any CR left by a Windows line ending.
    while (length_ > 0 &&
           std::isspace(static_cast<unsigned char>(buffer_[length_ - 1])) != 0) {
      buffer_[--length_] = '\0';
    }
    // Trim leading whitespace by shifting the view, not the buffer.
    std::size_t start = 0;
    while (start < length_ && std::isspace(static_cast<unsigned char>(buffer_[start])) != 0) {
      ++start;
    }

    const char*       text   = buffer_ + start;
    const std::size_t length = length_ - start;

    if (length == 0) {
      continue;  // blank line: ask again
    }
    if (equals(text, length, "quit") || equals(text, length, "exit")) {
      return Status::EndOfInput;
    }
    if (equals(text, length, "help") || equals(text, length, "?")) {
      print_help();
      continue;
    }

    // First word is the channel, the rest - if any - are its arguments.
    std::size_t split = 0;
    while (split < length && std::isspace(static_cast<unsigned char>(text[split])) == 0) {
      ++split;
    }
    std::size_t arguments = split;
    while (arguments < length && std::isspace(static_cast<unsigned char>(text[arguments])) != 0) {
      ++arguments;
    }

    input.channel   = StringView(text, split);
    input.arguments = StringView(text + arguments, length - arguments);
    return Status::Ok;
  }
}

Status ConsolePort::publish_state(StringView state) noexcept {
  std::cout << "state: ";
  std::cout.write(state.data(), static_cast<std::streamsize>(state.size()));
  std::cout << '\n';
  return Status::Ok;
}

Status ConsolePort::publish_error(StringView message) noexcept {
  std::cout.flush();
  std::cerr << "error: ";
  std::cerr.write(message.data(), static_cast<std::streamsize>(message.size()));
  std::cerr << '\n';
  return Status::Ok;
}

void ConsolePort::print_help() noexcept {
  std::cout << "channels (" << channel_count_ << "):\n";
  for (std::size_t i = 0; i < channel_count_; ++i) {
    std::cout << "  " << channels_[i].c_str() << '\n';
  }
  std::cout << "  quit\n";
}

}  // namespace fms::port
