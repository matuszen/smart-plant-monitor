#pragma once

#include <lwip/apps/mqtt.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

class MqttTransport final
{
public:
  using ConnectCallback = std::function<void(bool success)>;
  using MessageCallback = std::function<void(std::string_view topic, std::string_view payload)>;

  MqttTransport() = default;
  ~MqttTransport();

  MqttTransport(const MqttTransport&)                    = delete;
  auto operator=(const MqttTransport&) -> MqttTransport& = delete;
  MqttTransport(MqttTransport&&)                         = delete;
  auto operator=(MqttTransport&&) -> MqttTransport&      = delete;

  [[nodiscard]] auto init(const char* clientId, const char* host, uint16_t port, const char* user,
                          const char* pass) -> bool;
  void               connect(ConnectCallback cb);
  void               disconnect();

  [[nodiscard]] auto publish(const char* topic, const char* payload, bool retain = false) -> bool;
  [[nodiscard]] auto subscribe(const char* topic) -> bool;

  void               setOnMessage(MessageCallback cb);
  [[nodiscard]] auto isConnected() const -> bool;

private:
  static void mqttConnectionCb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status);
  static void mqttIncomingPublishCb(void* arg, const char* topic, uint32_t tot_len);
  static void mqttIncomingDataCb(void* arg, const uint8_t* data, uint16_t len, uint8_t flags);

  mqtt_client_t* client_{nullptr};
  std::string    host_;
  uint16_t       port_{0};
  std::string    clientId_;
  std::string    user_;
  std::string    pass_;

  bool            connected_{false};
  ConnectCallback connectCb_;
  MessageCallback messageCb_;

  std::string incomingTopic_;
  std::string incomingPayload_;
};
