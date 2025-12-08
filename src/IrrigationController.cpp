#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <hardware/gpio.h>
#include <pico/time.h>

#include "Config.h"
#include "IrrigationController.h"
#include "SensorManager.h"
#include "Types.h"

IrrigationController::IrrigationController(SensorManager* sensorManager)
  : sensorManager_(sensorManager)
{
}

IrrigationController::~IrrigationController()
{
  if (initialized_)
  {
    stopWatering();
  }
}

auto IrrigationController::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  printf("[IrrigationController] Initializing...\n");

  gpio_init(Config::RELAY_PIN);
  gpio_set_dir(Config::RELAY_PIN, true);
  activateRelay(false);

  initialized_ = true;
  printf("[IrrigationController] Initialization complete\n");
  return true;
}

void IrrigationController::update()
{
  if (not initialized_)
  {
    return;
  }

  if (mode_ == IrrigationMode::AUTOMATIC)
  {
    handleAutomaticMode();
  }

  if (isWatering_)
  {
    const auto now = to_ms_since_boot(get_absolute_time());
    if ((now - wateringStartTime_) >= wateringDuration_)
    {
      printf("[IrrigationController] Watering duration elapsed, stopping\n");
      stopWatering();
    }
  }
}

void IrrigationController::startWatering(const uint32_t durationMs)
{
  if (not initialized_ or isWatering_)
  {
    return;
  }

  if (not canStartWatering())
  {
    printf("[IrrigationController] Cannot start watering (cooldown period)\n");
    return;
  }

  wateringDuration_ =
    std::clamp(durationMs, Config::MIN_WATERING_DURATION_MS, Config::MAX_WATERING_DURATION_MS);

  printf("[IrrigationController] Starting watering for %u ms\n", wateringDuration_);

  isWatering_        = true;
  wateringStartTime_ = to_ms_since_boot(get_absolute_time());
  activateRelay(true);
}

void IrrigationController::stopWatering()
{
  if (not isWatering_)
  {
    return;
  }

  printf("[IrrigationController] Stopping watering\n");

  isWatering_       = false;
  lastWateringTime_ = to_ms_since_boot(get_absolute_time());
  activateRelay(false);
}

void IrrigationController::setMode(const IrrigationMode mode)
{
  if (mode_ == mode)
  {
    return;
  }

  printf("[IrrigationController] Changing mode to %d\n", static_cast<int>(mode));

  if (mode == IrrigationMode::OFF and isWatering_)
  {
    stopWatering();
  }

  mode_ = mode;
}

void IrrigationController::activateRelay(const bool enable)
{
  const auto state = Config::RELAY_ACTIVE_HIGH ? enable : not enable;
  gpio_put(Config::RELAY_PIN, state);
}

auto IrrigationController::shouldStartWatering() const -> bool
{
  if ((sensorManager_ == nullptr) or not sensorManager_->isInitialized())
  {
    return false;
  }

  const auto soilData = sensorManager_->readSoilMoisture();
  if (not soilData.valid)
  {
    return false;
  }

  const auto waterLevel = sensorManager_->readWaterLevel();
  if (not waterLevel.isValid())
  {
    return false;
  }
  if (waterLevel.isLow())
  {
    return false;
  }

  return soilData.percentage < Config::SOIL_MOISTURE_DRY_THRESHOLD;
}

auto IrrigationController::canStartWatering() const -> bool
{
  if (lastWateringTime_ == 0)
  {
    return true;
  }

  const auto now           = to_ms_since_boot(get_absolute_time());
  const auto timeSinceLast = now - lastWateringTime_;

  return timeSinceLast >= Config::WATERING_COOLDOWN_MS;
}

void IrrigationController::handleAutomaticMode()
{
  if (isWatering_)
  {
    const auto soilData = sensorManager_->readSoilMoisture();
    if (soilData.valid and soilData.percentage >= Config::SOIL_MOISTURE_WET_THRESHOLD)
    {
      printf("[IrrigationController] Soil moisture sufficient, stopping early\n");
      stopWatering();
    }
    return;
  }

  if (shouldStartWatering() and canStartWatering())
  {
    startWatering();
  }
}
