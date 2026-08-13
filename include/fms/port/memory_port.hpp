// SPDX-License-Identifier: MIT
//
// An in-memory IPort for tests and dry runs.  Input is queued by the caller,
// output is recorded for inspection.  Header only, fixed capacity, no heap.
#ifndef FMS_PORT_MEMORY_PORT_HPP
#define FMS_PORT_MEMORY_PORT_HPP

#include <etl/vector.h>

#include "fms/port.hpp"

namespace fms::port {

template <std::size_t QueueDepth = 16, std::size_t HistoryDepth = 32>
class MemoryPort final : public IPort {
 public:
  MemoryPort() noexcept = default;

  // ---- IPort --------------------------------------------------------------
  Status open() noexcept override {
    open_ = true;
    return Status::Ok;
  }

  Status close() noexcept override {
    open_ = false;
    return Status::Ok;
  }

  Status listen(StringView channel) noexcept override {
    if (!open_) {
      return Status::NotOpen;
    }
    if (listening_.full()) {
      return Status::CapacityExceeded;
    }
    Channel entry;
    if (!assign_checked(entry, channel)) {
      return Status::ChannelTooLong;
    }
    listening_.push_back(entry);
    return Status::Ok;
  }

  Status receive(StringView& channel, std::uint32_t /*timeout_ms*/) noexcept override {
    if (!open_) {
      return Status::NotOpen;
    }
    if (inbox_.empty()) {
      return end_of_input_ ? Status::EndOfInput : Status::Timeout;
    }
    current_ = inbox_.front();
    inbox_.erase(inbox_.begin());
    channel = view(current_);
    return Status::Ok;
  }

  Status publish_state(StringView state) noexcept override {
    return record(states_, state);
  }

  Status publish_error(StringView message) noexcept override {
    return record(errors_, message);
  }

  // ---- test helpers -------------------------------------------------------

  /// Queues input as if it had arrived on `channel`.
  Status inject(StringView channel) noexcept {
    if (inbox_.full()) {
      return Status::CapacityExceeded;
    }
    Channel entry;
    if (!assign_checked(entry, channel)) {
      return Status::ChannelTooLong;
    }
    inbox_.push_back(entry);
    return Status::Ok;
  }

  /// Makes the next receive() on an empty queue report EndOfInput.
  void set_end_of_input(bool value = true) noexcept { end_of_input_ = value; }

  const etl::vector<Message, HistoryDepth>& states() const noexcept { return states_; }
  const etl::vector<Message, HistoryDepth>& errors() const noexcept { return errors_; }
  const etl::vector<Channel, limits::kMaxTriggers>& listening() const noexcept {
    return listening_;
  }

  StringView last_state() const noexcept {
    return states_.empty() ? StringView{} : view(states_.back());
  }
  StringView last_error_message() const noexcept {
    return errors_.empty() ? StringView{} : view(errors_.back());
  }

  void clear_history() noexcept {
    states_.clear();
    errors_.clear();
  }

  bool is_open() const noexcept { return open_; }

 private:
  template <typename TVector>
  Status record(TVector& into, StringView text) noexcept {
    if (!open_) {
      return Status::NotOpen;
    }
    if (into.full()) {
      into.erase(into.begin());  // keep the most recent history
    }
    Message entry;
    append_clipped(entry, text);
    into.push_back(entry);
    return Status::Ok;
  }

  bool open_         = false;
  bool end_of_input_ = false;

  Channel current_{};  ///< storage the view handed to receive() points at

  etl::vector<Channel, QueueDepth>          inbox_{};
  etl::vector<Channel, limits::kMaxTriggers> listening_{};
  etl::vector<Message, HistoryDepth>        states_{};
  etl::vector<Message, HistoryDepth>        errors_{};
};

}  // namespace fms::port

#endif  // FMS_PORT_MEMORY_PORT_HPP
