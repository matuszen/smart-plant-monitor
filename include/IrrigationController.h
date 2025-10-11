#pragma once

#include <algorithm>
#include <memory>

#include "Config.h"
#include "SensorManager.h"
#include "Types.h"

class IrrigationController final
{
public:
  explicit IrrigationController(SensorManager* sensorManager);
  ~IrrigationController();

  IrrigationController(const IrrigationController&)                        = delete;
  auto operator=(const IrrigationController&) -> IrrigationController&     = delete;
  IrrigationController(IrrigationController&&) noexcept                    = delete;
  auto operator=(IrrigationController&&) noexcept -> IrrigationController& = delete;

  auto init() -> bool;
  void update();

  void startWatering(uint32_t durationMs = 5000);
  void stopWatering();

  void               setMode(IrrigationMode mode);
  [[nodiscard]] auto getMode() const -> IrrigationMode
  {
    return mode_;
  }

  [[nodiscard]] auto isWatering() const -> bool
  {
    return isWatering_;
  }
  [[nodiscard]] auto isInitialized() const -> bool
  {
    return initialized_;
  }

private:
  SensorManager* sensorManager_;
  IrrigationMode mode_{IrrigationMode::AUTOMATIC};
  bool           initialized_{false};
  bool           isWatering_{false};

  uint32_t wateringStartTime_{0};
  uint32_t wateringDuration_{5000};
  uint32_t lastWateringTime_{0};

  static void        activateRelay(bool enable);
  [[nodiscard]] auto shouldStartWatering() const -> bool;
  [[nodiscard]] auto canStartWatering() const -> bool;
  void               handleAutomaticMode();
};
