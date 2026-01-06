#pragma once

#include "SensorController.hpp"

#include "Config.hpp"
#include "Types.hpp"

#include <cstdint>

class IrrigationController final
{
public:
  explicit IrrigationController(SensorController& sensorController);
  ~IrrigationController();

  IrrigationController(const IrrigationController&)                    = delete;
  auto operator=(const IrrigationController&) -> IrrigationController& = delete;
  IrrigationController(IrrigationController&&)                         = delete;
  auto operator=(IrrigationController&&) -> IrrigationController&      = delete;

  auto init() -> bool;
  void update(const SensorData& sensorData);
  void checkWateringTimeout();

  void startWatering(uint32_t durationMs = Config::DEFAULT_WATERING_DURATION_MS);
  void stopWatering();

  void setMode(IrrigationMode mode);
  auto getMode() const -> IrrigationMode;

  auto isWatering() const -> bool;

  auto isInitialized() const -> bool;

  auto nextSleepHintMs() const -> uint32_t;

private:
  bool              initialized_            = false;
  bool              isWatering_             = false;
  float             lastSoilPercentage_     = Config::SOIL_MOISTURE_WET_THRESHOLD;
  uint32_t          wateringStartTime_      = 0;
  uint32_t          lastWateringTime_       = 0;
  uint32_t          nextWateringEstimateMs_ = 0;
  uint32_t          wateringDuration_       = Config::DEFAULT_WATERING_DURATION_MS;
  uint32_t          sleepHintMs_            = Config::IRRIGATION_ACTIVE_TICK_MS;
  IrrigationMode    mode_                   = IrrigationMode::EVAPOTRANSPIRATION;
  SensorController& sensorController_;
  SensorData        lastSensorData_;

  auto        canStartWatering() const -> bool;
  auto        shouldStartWatering() const -> bool;
  static void activateWaterPump(bool enable);

  void        handleHumidityBasedMode(const SensorData& data);
  void        handleEvapotranspirationMode(const SensorData& data);
  static auto computeEvapoLossPerHour(const EnvironmentData& env) -> float;
};
