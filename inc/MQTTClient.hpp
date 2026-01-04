#pragma once

#include "IrrigationController.hpp"
#include "SensorManager.hpp"
#include "Types.hpp"

#include <lwip/apps/mqtt.h>
#include <lwip/arch.h>
#include <lwip/ip_addr.h>

#include <array>
#include <cstdint>
#include <string_view>

class MQTTClient final
{
public:
  MQTTClient(SensorManager* sensorManager, IrrigationController* irrigationController);
  ~MQTTClient();

  MQTTClient(const MQTTClient&)                    = delete;
  auto operator=(const MQTTClient&) -> MQTTClient& = delete;
  MQTTClient(MQTTClient&&)                         = delete;
  auto operator=(MQTTClient&&) -> MQTTClient&      = delete;

  [[nodiscard]] auto init(const MqttConfig& config) -> bool;
  void               loop(uint32_t nowMs);
  void               publishSensorState(uint32_t nowMs, const SensorData& data, bool watering, bool force = false);

  [[nodiscard]] auto getSensorManager() const noexcept -> SensorManager*
  {
    return sensorManager_;
  }

  [[nodiscard]] auto getIrrigationController() const noexcept -> IrrigationController*
  {
    return irrigationController_;
  }

  [[nodiscard]] auto isConnected() const noexcept -> bool;

  void setWifiReady(bool ready) noexcept
  {
    wifiReady_ = ready;
  }

private:
  enum class ConnectionState : uint8_t
  {
    DISCONNECTED,
    RESOLVING_DNS,
    CONNECTING,
    CONNECTED
  };

  void ensureMqtt(uint32_t nowMs);
  void publishDiscovery();
  void publishAvailability(bool online);
  void publishSensorDiscovery(std::string_view component, std::string_view objectId, std::string_view name,
                              std::string_view valueTemplate, std::string_view unit, std::string_view deviceClass = {});
  void publishSwitchDiscovery();

  void connectMqtt();
  void subscribeToCommands();
  void handleCommand(std::string_view payload);

  [[nodiscard]] auto resolveBrokerIp() -> bool;
  void               handleDnsResult(const ip_addr_t* result);

  static void mqttConnectionCb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status);
  static void mqttIncomingPublishCb(void* arg, const char* topic, uint32_t tot_len);
  static void mqttIncomingDataCb(void* arg, const uint8_t* data, uint16_t len, uint8_t flags);
  static void dnsFoundCb(const char* name, const ip_addr_t* ipaddr, void* arg);

  SensorManager*        sensorManager_;
  IrrigationController* irrigationController_;
  MqttConfig            config_{};

  mqtt_client_t*  mqttClient_{nullptr};
  ip_addr_t       brokerIp_{};
  bool            brokerIpValid_{false};
  bool            wifiReady_{false};
  ConnectionState connectionState_{ConnectionState::DISCONNECTED};
  bool            discoveryPublished_{false};

  uint32_t lastMqttAttempt_{0};
  uint32_t lastPublish_{0};
  uint32_t mqttBackoffMultiplier_{1};

  SensorData lastData_{};
  bool       hasData_{false};

  std::array<char, 64> availabilityTopic_{};
  std::array<char, 64> stateTopic_{};
  std::array<char, 64> commandTopic_{};

  std::array<char, 64>  lastIncomingTopic_{};
  std::array<char, 128> incomingBuffer_{};
};
