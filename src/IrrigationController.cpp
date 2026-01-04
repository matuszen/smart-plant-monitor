#include "IrrigationController.hpp"
#include "Config.hpp"
#include "SensorManager.hpp"
#include "Types.hpp"

#include <hardware/gpio.h>
#include <pico/time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

IrrigationController::IrrigationController(SensorManager* sensorManager) : sensorManager_(sensorManager)
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

  gpio_init(Config::PUMP_CONTROL_PIN);
  gpio_set_dir(Config::PUMP_CONTROL_PIN, GPIO_OUT);
  activateRelay(false);

  initialized_ = true;
  printf("[IrrigationController] Initialization complete\n");
  return true;
}

void IrrigationController::update(const SensorData& sensorData)
{
  if (not initialized_)
  {
    return;
  }

  lastSensorData_ = sensorData;
  sleepHintMs_    = Config::IRRIGATION_ACTIVE_TICK_MS;

  if (mode_ == IrrigationMode::HUMIDITY)
  {
    handleHumidityBasedMode(sensorData);
  }

  if (mode_ == IrrigationMode::EVAPOTRANSPIRATION)
  {
    handleEvapotranspirationMode(sensorData);
  }

  checkWateringTimeout();
}

void IrrigationController::checkWateringTimeout()
{
  if (not isWatering_)
  {
    return;
  }

  const auto now = to_ms_since_boot(get_absolute_time());
  if ((now - wateringStartTime_) >= wateringDuration_)
  {
    printf("[IrrigationController] Watering duration elapsed, stopping\n");
    stopWatering();
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

  wateringDuration_ = std::clamp(durationMs, Config::MIN_WATERING_DURATION_MS, Config::MAX_WATERING_DURATION_MS);

  printf("Turning water pump ON...\n");
  printf("[IrrigationController] Starting watering for %u ms\n", wateringDuration_);

  isWatering_        = true;
  wateringStartTime_ = to_ms_since_boot(get_absolute_time());
  sleepHintMs_       = Config::IRRIGATION_ACTIVE_TICK_MS;
  activateRelay(true);
}

void IrrigationController::stopWatering()
{
  if (not isWatering_)
  {
    return;
  }

  printf("Turning water pump OFF...\n");
  printf("[IrrigationController] Stopping watering\n");

  isWatering_       = false;
  lastWateringTime_ = to_ms_since_boot(get_absolute_time());
  sleepHintMs_      = Config::IRRIGATION_ACTIVE_TICK_MS;
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

auto IrrigationController::nextSleepHintMs() const -> uint32_t
{
  if (isWatering_)
  {
    return Config::IRRIGATION_ACTIVE_TICK_MS;
  }
  return sleepHintMs_;
}

void IrrigationController::activateRelay(const bool enable)
{
  gpio_put(Config::PUMP_CONTROL_PIN, enable);
}

auto IrrigationController::shouldStartWatering() const -> bool
{
  if ((sensorManager_ == nullptr) or not sensorManager_->isInitialized())
  {
    return false;
  }

  const auto soilData = sensorManager_->readSoilMoisture();
  if (not soilData.valid) [[unlikely]]
  {
    return false;
  }

  const auto waterLevel = sensorManager_->readWaterLevel();
  if (not waterLevel.isValid()) [[unlikely]]
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

void IrrigationController::handleHumidityBasedMode(const SensorData& sensorData)
{
  if (isWatering_)
  {
    if (sensorData.soil.valid and sensorData.soil.percentage >= Config::SOIL_MOISTURE_WET_THRESHOLD)
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

void IrrigationController::handleEvapotranspirationMode(const SensorData& sensorData)
{
  const auto& env        = sensorData.environment;
  const auto& soil       = sensorData.soil;
  const auto& waterLevel = sensorData.water;

  if (isWatering_)
  {
    if (soil.valid and soil.percentage >= Config::SOIL_MOISTURE_WET_THRESHOLD)
    {
      printf("[IrrigationController] Soil moisture sufficient, stopping early\n");
      stopWatering();
    }
    return;
  }

  if ((not env.isValid()) or (not soil.valid)) [[unlikely]]
  {
    handleHumidityBasedMode(sensorData);
    return;
  }

  if ((not waterLevel.isValid()) or waterLevel.isLow())
  {
    return;
  }

  lastSoilPercentage_ = soil.percentage;

  if (soil.percentage < Config::SOIL_MOISTURE_DRY_THRESHOLD)
  {
    startWatering();
    return;
  }

  const auto dropPerHour = computeEvapoLossPerHour(env);
  if (dropPerHour <= 0.0F)
  {
    sleepHintMs_            = Config::EVAPO_MAX_SLEEP_MS;
    nextWateringEstimateMs_ = to_ms_since_boot(get_absolute_time()) + sleepHintMs_;
    return;
  }

  const auto marginPct        = soil.percentage - Config::SOIL_MOISTURE_DRY_THRESHOLD;
  const auto hoursUntilDry    = marginPct / dropPerHour;
  const auto projectedSleepMs = hoursUntilDry * 3'600'000.0;
  const auto clampedSleepMs   = std::clamp(projectedSleepMs, static_cast<double>(Config::IRRIGATION_ACTIVE_TICK_MS),
                                           static_cast<double>(Config::EVAPO_MAX_SLEEP_MS));

  sleepHintMs_            = static_cast<uint32_t>(clampedSleepMs);
  nextWateringEstimateMs_ = to_ms_since_boot(get_absolute_time()) + sleepHintMs_;

  printf("[IrrigationController] ET forecast: %.2f%%/h, next check in %u ms (eta %u)\n", dropPerHour, sleepHintMs_,
         nextWateringEstimateMs_);
}

auto IrrigationController::computeEvapoLossPerHour(const EnvironmentData& env) -> float
{
  const auto tempC          = env.temperature;
  const auto humidity       = std::clamp(env.humidity, 0.0F, 100.0F);
  const auto satVapor       = 0.6108F * std::exp((17.27F * tempC) / (tempC + 237.3F));
  const auto vpd            = satVapor * (1.0F - (humidity / 100.0F));
  const auto pressureK      = env.pressure * 0.001F;
  const auto pressureFactor = std::max(0.7F, pressureK / 101.325F);

  const auto etMmPerHour = std::max(0.0F, (0.12F + 0.45F * vpd) * pressureFactor);
  const auto pctPerHour  = (etMmPerHour / Config::EVAPO_SOIL_BUCKET_MM) * 100.0F;

  return std::max(pctPerHour, Config::EVAPO_MIN_DROP_PER_HOUR_PCT);
}
