#pragma once

#include "Types.hpp"

#include <lwip/apps/mqtt.h>
#include <lwip/arch.h>
#include <lwip/ip_addr.h>

#include <array>
#include <cstdint>

class SensorManager;
class IrrigationController;

class HomeAssistantClient final
{
public:
  HomeAssistantClient(SensorManager* sensorManager, IrrigationController* irrigationController);
  ~HomeAssistantClient();

  HomeAssistantClient(const HomeAssistantClient&)                    = delete;
  auto operator=(const HomeAssistantClient&) -> HomeAssistantClient& = delete;
  HomeAssistantClient(HomeAssistantClient&&)                         = delete;
  auto operator=(HomeAssistantClient&&) -> HomeAssistantClient&      = delete;

  auto init() -> bool;
  void loop(uint32_t nowMs);
  void publishSensorState(uint32_t nowMs, const SensorData& data, bool watering, bool force = false);

  void setControllers(SensorManager* sensorManager, IrrigationController* irrigationController)
  {
    sensorManager_        = sensorManager;
    irrigationController_ = irrigationController;
  }

  [[nodiscard]] auto getSensorManager() const noexcept -> SensorManager*
  {
    return sensorManager_;
  }

  [[nodiscard]] auto getIrrigationController() const noexcept -> IrrigationController*
  {
    return irrigationController_;
  }

  void setWifiReady(bool ready) noexcept
  {
    wifiReady_ = ready;
  }

private:
  void ensureMqtt(uint32_t nowMs);
  void publishDiscovery();
  void publishAvailability(bool online);
  void publishSensorDiscovery(const char* component, const char* objectId, const char* name, const char* valueTemplate,
                              const char* unit, const char* deviceClass = nullptr);
  void publishSwitchDiscovery();

  void connectMqtt();
  void subscribeToCommands();
  void handleCommand(const char* payload);

  auto resolveBrokerIp() -> bool;
  void handleDnsResult(const ip_addr_t* result);

  static void mqttConnectionCb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status);
  static void mqttIncomingPublishCb(void* arg, const char* topic, uint32_t tot_len);
  static void mqttIncomingDataCb(void* arg, const uint8_t* data, uint16_t len, uint8_t flags);
  static void dnsFoundCb(const char* name, const ip_addr_t* ipaddr, void* arg);

  SensorManager*        sensorManager_;
  IrrigationController* irrigationController_;

  mqtt_client_t* mqttClient_{nullptr};
  ip_addr_t      brokerIp_{};
  bool           brokerIpValid_{false};
  bool           wifiReady_{false};
  bool           mqttConnected_{false};
  bool           discoveryPublished_{false};

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
