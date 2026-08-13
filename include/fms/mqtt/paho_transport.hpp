// SPDX-License-Identifier: MIT
//
// ITransport on top of paho.mqtt.c (the synchronous MQTTClient API).
//
// Why the C client and not paho.mqtt.cpp: the C++ wrapper reports errors by
// throwing, which this project does not allow.  paho.mqtt.c returns codes.
//
// Why the *synchronous* client: MQTTClient_receive() lets us pump the socket
// from our own loop, so there is no paho callback thread, no locking, and the
// state machine runs on the same thread as the main loop.
//
// Allocation note: paho mallocs internally while a message is in flight (third
// party code, outside our control).  This layer copies each message into fixed
// members and frees paho's buffers before returning, so nothing heap allocated
// reaches the state machine and nothing this class owns is on the heap.
#ifndef FMS_MQTT_PAHO_TRANSPORT_HPP
#define FMS_MQTT_PAHO_TRANSPORT_HPP

#include "fms/model.hpp"
#include "fms/mqtt/transport.hpp"

namespace fms::mqtt {

class PahoTransport final : public ITransport {
 public:
  PahoTransport() noexcept = default;
  ~PahoTransport() override;

  PahoTransport(const PahoTransport&) = delete;
  PahoTransport& operator=(const PahoTransport&) = delete;

  /// Creates the underlying client.  Setup phase: the only call that may
  /// allocate (inside paho).  `config` is copied, not referenced.
  Status open(const MqttConfig& config) noexcept;

  Status connect() noexcept override;
  Status disconnect() noexcept override;
  bool   connected() const noexcept override;

  Status subscribe(StringView topic, std::uint8_t qos) noexcept override;
  Status publish(StringView topic, StringView payload, std::uint8_t qos,
                 bool retain) noexcept override;
  Status poll(std::uint32_t timeout_ms) noexcept override;

  void set_handler(MessageHandler handler, void* user) noexcept override;

  const char* last_error() const noexcept override { return last_error_.c_str(); }

  /// paho's own return code from the most recent failed call.
  int last_code() const noexcept { return last_code_; }

 private:
  void record_error(int code, const char* what) noexcept;

  void*          client_ = nullptr;  ///< MQTTClient, kept opaque so paho headers stay in the .cpp
  MqttConfig     config_{};
  MessageHandler handler_      = nullptr;
  void*          handler_user_ = nullptr;
  bool           opened_       = false;

  // Fixed staging areas - one message at a time, no heap.  etl::string keeps a
  // terminator, so c_str() can be handed straight to the paho C API.
  Topic   rx_topic_{};
  Payload rx_payload_{};
  Topic   tx_topic_{};
  Payload tx_payload_{};

  Payload last_error_{"none"};
  int     last_code_ = 0;
};

}  // namespace fms::mqtt

#endif  // FMS_MQTT_PAHO_TRANSPORT_HPP
