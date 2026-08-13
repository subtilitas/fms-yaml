// SPDX-License-Identifier: MIT
#include "fms/mqtt/paho_transport.hpp"

#include <cstring>

#include <MQTTClient.h>

namespace fms::mqtt {
namespace {

Status from_paho(int code) noexcept {
  switch (code) {
    case MQTTCLIENT_SUCCESS:      return Status::Ok;
    case MQTTCLIENT_DISCONNECTED: return Status::NotConnected;
    default:                      return Status::TransportError;
  }
}

}  // namespace

PahoTransport::~PahoTransport() {
  if (client_ != nullptr) {
    MQTTClient handle = static_cast<MQTTClient>(client_);
    if (MQTTClient_isConnected(handle)) {
      MQTTClient_disconnect(handle, 1000);
    }
    MQTTClient_destroy(&handle);
    client_ = nullptr;
  }
}

Status PahoTransport::open(const MqttConfig& config) noexcept {
  if (opened_) {
    return Status::AlreadyInitialised;
  }
  config_ = config;

  MQTTClient handle = nullptr;
  const int  rc = MQTTClient_create(&handle, config_.broker.c_str(), config_.client_id.c_str(),
                                   MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTCLIENT_SUCCESS) {
    record_error(rc, "MQTTClient_create");
    return Status::TransportError;
  }

  client_ = handle;
  opened_ = true;
  return Status::Ok;
}

Status PahoTransport::connect() noexcept {
  if (!opened_ || client_ == nullptr) {
    return Status::NotInitialised;
  }

  MQTTClient_connectOptions options = MQTTClient_connectOptions_initializer;
  options.keepAliveInterval = static_cast<int>(config_.keep_alive_s);
  options.cleansession      = config_.clean_session ? 1 : 0;
  options.connectTimeout    = static_cast<int>(config_.connect_timeout_ms / 1000u);
  if (options.connectTimeout <= 0) {
    options.connectTimeout = 1;
  }

  const int rc = MQTTClient_connect(static_cast<MQTTClient>(client_), &options);
  if (rc != MQTTCLIENT_SUCCESS) {
    record_error(rc, "MQTTClient_connect");
    return Status::TransportError;
  }
  return Status::Ok;
}

Status PahoTransport::disconnect() noexcept {
  if (client_ == nullptr) {
    return Status::NotInitialised;
  }
  const int rc = MQTTClient_disconnect(static_cast<MQTTClient>(client_),
                                       static_cast<int>(config_.connect_timeout_ms));
  if (rc != MQTTCLIENT_SUCCESS) {
    record_error(rc, "MQTTClient_disconnect");
    return from_paho(rc);
  }
  return Status::Ok;
}

bool PahoTransport::connected() const noexcept {
  return client_ != nullptr && MQTTClient_isConnected(static_cast<MQTTClient>(client_)) != 0;
}

Status PahoTransport::subscribe(StringView topic, std::uint8_t qos) noexcept {
  if (!connected()) {
    return Status::NotConnected;
  }
  if (!assign_checked(tx_topic_, topic)) {
    return Status::TopicTooLong;
  }

  const int rc =
      MQTTClient_subscribe(static_cast<MQTTClient>(client_), tx_topic_.c_str(), static_cast<int>(qos));
  if (rc != MQTTCLIENT_SUCCESS) {
    record_error(rc, "MQTTClient_subscribe");
    return from_paho(rc);
  }
  return Status::Ok;
}

Status PahoTransport::publish(StringView topic, StringView payload, std::uint8_t qos,
                              bool retain) noexcept {
  if (!connected()) {
    return Status::NotConnected;
  }
  if (!assign_checked(tx_topic_, topic)) {
    return Status::TopicTooLong;
  }
  tx_payload_.clear();
  append_clipped(tx_payload_, payload);

  MQTTClient_message message = MQTTClient_message_initializer;
  message.payload    = const_cast<char*>(tx_payload_.c_str());
  message.payloadlen = static_cast<int>(tx_payload_.size());
  message.qos        = static_cast<int>(qos);
  message.retained   = retain ? 1 : 0;

  MQTTClient_deliveryToken token = 0;
  const int rc = MQTTClient_publishMessage(static_cast<MQTTClient>(client_), tx_topic_.c_str(),
                                           &message, &token);
  if (rc != MQTTCLIENT_SUCCESS) {
    record_error(rc, "MQTTClient_publishMessage");
    return from_paho(rc);
  }
  if (qos > 0) {
    MQTTClient_waitForCompletion(static_cast<MQTTClient>(client_), token,
                                 static_cast<unsigned long>(config_.connect_timeout_ms));
  }
  return Status::Ok;
}

Status PahoTransport::poll(std::uint32_t timeout_ms) noexcept {
  if (!connected()) {
    return Status::NotConnected;
  }

  char*               topic_name = nullptr;
  int                 topic_len  = 0;
  MQTTClient_message* message    = nullptr;

  const int rc = MQTTClient_receive(static_cast<MQTTClient>(client_), &topic_name, &topic_len,
                                    &message, static_cast<unsigned long>(timeout_ms));

  if (rc != MQTTCLIENT_SUCCESS && rc != MQTTCLIENT_TOPICNAME_TRUNCATED) {
    record_error(rc, "MQTTClient_receive");
    return from_paho(rc);
  }
  if (message == nullptr) {
    if (topic_name != nullptr) {
      MQTTClient_free(topic_name);
    }
    return Status::Timeout;  // nothing arrived: the normal idle path
  }

  // paho allocated these two buffers.  Copy into fixed storage and hand them
  // straight back, so nothing heap allocated reaches the state machine.
  const std::size_t name_length =
      (topic_len > 0) ? static_cast<std::size_t>(topic_len) : std::strlen(topic_name);

  const bool topic_ok = assign_checked(rx_topic_, StringView(topic_name, name_length));
  rx_payload_.clear();
  append_clipped(rx_payload_, StringView(static_cast<const char*>(message->payload),
                                         static_cast<std::size_t>(message->payloadlen)));
  const int  qos      = message->qos;
  const bool retained = message->retained != 0;

  MQTTClient_freeMessage(&message);
  MQTTClient_free(topic_name);

  if (!topic_ok) {
    // A topic we cannot even store is a topic we never subscribed to.
    record_error(0, "inbound topic exceeds FMS_MAX_TOPIC_LENGTH");
    return Status::TopicTooLong;
  }

  if (handler_ != nullptr) {
    InboundMessage inbound;
    inbound.topic    = view(rx_topic_);
    inbound.payload  = view(rx_payload_);
    inbound.qos      = static_cast<std::uint8_t>(qos);
    inbound.retained = retained;
    handler_(handler_user_, inbound);
  }
  return Status::Ok;
}

void PahoTransport::set_handler(MessageHandler handler, void* user) noexcept {
  handler_      = handler;
  handler_user_ = user;
}

void PahoTransport::record_error(int code, const char* what) noexcept {
  last_code_ = code;
  last_error_.clear();
  append_clipped(last_error_, StringView(what, std::strlen(what)));
}

}  // namespace fms::mqtt
