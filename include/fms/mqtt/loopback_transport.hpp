// SPDX-License-Identifier: MIT
//
// In-memory ITransport for tests and dry runs.  Inbound messages are queued by
// the caller, outbound publishes are recorded for inspection.  Fixed capacity,
// header only, no heap, no broker.
#ifndef FMS_MQTT_LOOPBACK_TRANSPORT_HPP
#define FMS_MQTT_LOOPBACK_TRANSPORT_HPP

#include <etl/vector.h>

#include "fms/mqtt/transport.hpp"

namespace fms::mqtt {

template <std::size_t QueueDepth = 16, std::size_t HistoryDepth = 32>
class LoopbackTransport final : public ITransport {
 public:
  struct Record {
    Topic        topic{};
    Payload      payload{};
    std::uint8_t qos    = 0;
    bool         retain = false;
  };

  LoopbackTransport() noexcept = default;

  // ---- ITransport ---------------------------------------------------------
  Status connect() noexcept override {
    connected_ = true;
    return Status::Ok;
  }

  Status disconnect() noexcept override {
    connected_ = false;
    return Status::Ok;
  }

  bool connected() const noexcept override { return connected_; }

  Status subscribe(StringView topic, std::uint8_t qos) noexcept override {
    if (!connected_) {
      return Status::NotConnected;
    }
    if (subscriptions_.full()) {
      return Status::CapacityExceeded;
    }
    Record record;
    if (!assign_checked(record.topic, topic)) {
      return Status::TopicTooLong;
    }
    record.qos = qos;
    subscriptions_.push_back(record);
    return Status::Ok;
  }

  Status publish(StringView topic, StringView payload, std::uint8_t qos,
                 bool retain) noexcept override {
    if (!connected_) {
      return Status::NotConnected;
    }
    Record record;
    if (!assign_checked(record.topic, topic)) {
      return Status::TopicTooLong;
    }
    append_clipped(record.payload, payload);
    record.qos    = qos;
    record.retain = retain;

    if (published_.full()) {
      published_.erase(published_.begin());  // keep the most recent history
    }
    published_.push_back(record);
    return Status::Ok;
  }

  Status poll(std::uint32_t /*timeout_ms*/) noexcept override {
    if (inbox_.empty()) {
      return Status::Timeout;
    }
    const Record record = inbox_.front();
    inbox_.erase(inbox_.begin());

    if (handler_ != nullptr) {
      InboundMessage message;
      message.topic   = view(record.topic);
      message.payload = view(record.payload);
      message.qos     = record.qos;
      handler_(handler_user_, message);
    }
    return Status::Ok;
  }

  void set_handler(MessageHandler handler, void* user) noexcept override {
    handler_      = handler;
    handler_user_ = user;
  }

  const char* last_error() const noexcept override { return "none"; }

  // ---- test helpers -------------------------------------------------------

  /// Queues a message as if the broker had delivered it.
  Status inject(StringView topic, StringView payload = StringView{}) noexcept {
    if (inbox_.full()) {
      return Status::CapacityExceeded;
    }
    Record record;
    if (!assign_checked(record.topic, topic)) {
      return Status::TopicTooLong;
    }
    append_clipped(record.payload, payload);
    inbox_.push_back(record);
    return Status::Ok;
  }

  const etl::vector<Record, HistoryDepth>& published() const noexcept { return published_; }
  const etl::vector<Record, HistoryDepth>& subscriptions() const noexcept {
    return subscriptions_;
  }

  void clear_published() noexcept { published_.clear(); }

  /// Last payload published on `topic`, or an empty view.
  StringView last_published_on(StringView topic) const noexcept {
    for (auto it = published_.rbegin(); it != published_.rend(); ++it) {
      if (view(it->topic) == topic) {
        return view(it->payload);
      }
    }
    return StringView{};
  }

 private:
  bool           connected_    = false;
  MessageHandler handler_      = nullptr;
  void*          handler_user_ = nullptr;

  etl::vector<Record, QueueDepth>   inbox_{};
  etl::vector<Record, HistoryDepth> published_{};
  etl::vector<Record, HistoryDepth> subscriptions_{};
};

}  // namespace fms::mqtt

#endif  // FMS_MQTT_LOOPBACK_TRANSPORT_HPP
