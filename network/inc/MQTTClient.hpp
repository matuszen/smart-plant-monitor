#pragma once

#include "MqttTransport.hpp"

#include "IrrigationController.hpp"
#include "SensorController.hpp"
#include "Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

class MQTTClient final
{
public:
  MQTTClient(SensorController* sensorController, IrrigationController* irrigationController);
  ~MQTTClient();

  MQTTClient(const MQTTClient&)                    = delete;
  auto operator=(const MQTTClient&) -> MQTTClient& = delete;
  MQTTClient(MQTTClient&&)                         = delete;
  auto operator=(MQTTClient&&) -> MQTTClient&      = delete;

  [[nodiscard]] auto init(const MqttConfig& config) -> bool;
  void               loop(uint32_t nowMs);
  void               publishSensorState(uint32_t nowMs, const SensorData& data, bool watering, bool force = false);
  void               publishActivity(std::string_view message);

  void               setPublishInterval(uint32_t intervalMs);
  [[nodiscard]] auto getPublishInterval() const -> uint32_t
  {
    return config_.publishIntervalMs;
  }

  [[nodiscard]] auto getIrrigationController() const -> IrrigationController*
  {
    return irrigationController_;
  }

  [[nodiscard]] auto getSensorController() const -> SensorController*
  {
    return sensorController_;
  }

  [[nodiscard]] auto isConnected() const -> bool;

  void setWifiReady(bool ready)
  {
    wifiReady_ = ready;
  }

  void requestUpdate()
  {
    updateRequest_ = true;
  }
  [[nodiscard]] auto isUpdateRequested() const -> bool
  {
    return updateRequest_;
  }
  void clearUpdateRequest()
  {
    updateRequest_ = false;
  }

private:
  void ensureMqtt(uint32_t nowMs);
  void publishDiscovery();
  void publishAvailability(bool online);
  void publishSensorDiscovery(std::string_view component, std::string_view objectId, std::string_view name,
                              std::string_view valueTemplate, std::string_view unit, std::string_view deviceClass = {});
  void publishSelectDiscovery();
  void publishButtonDiscovery();
  void publishNumberDiscovery();
  void publishTextDiscovery();
  void publishIntervalState();
  void publishUpdateTriggerDiscovery();

  void connectMqtt();
  void subscribeToCommands();
  void handleCommand(std::string_view topic, std::string_view payload);
  void handleModeCommand(std::string_view payload);
  void handleTriggerCommand(std::string_view payload);
  void handleIntervalCommand(std::string_view payload);

  MqttTransport transport_;
  MqttConfig    config_;

  SensorController*     sensorController_{nullptr};
  IrrigationController* irrigationController_{nullptr};

  bool wifiReady_{false};
  bool updateRequest_{false};
  bool needsDiscovery_{true};
  bool needsInitialPublish_{true};
  bool hasData_{false};

  SensorData lastData_{};
  uint32_t   lastPublish_{0};
  uint32_t   lastReconnectAttempt_{0};

  std::array<char, 128> availabilityTopic_{};
  std::array<char, 128> stateTopic_{};
  std::array<char, 128> commandTopic_{};
  std::array<char, 128> modeCommandTopic_{};
  std::array<char, 128> modeStateTopic_{};
  std::array<char, 128> triggerCommandTopic_{};
  std::array<char, 128> updateCommandTopic_{};
  std::array<char, 128> intervalCommandTopic_{};
  std::array<char, 128> intervalStateTopic_{};
  std::array<char, 128> activityStateTopic_{};
};
