#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/structs/io_bank0.h>
#include <pico/time.h>

#include "BME280.h"
#include "Config.h"
#include "SensorManager.h"
#include "Types.h"
#include "WaterLevelSensor.h"

namespace
{

[[nodiscard]] auto resolveI2CInstance(const uint8_t instance) -> i2c_inst_t*
{
  switch (instance)
  {
    case 0:
      return i2c0;
#if defined(i2c1)
    case 1:
      return i2c1;
#endif
    default:
      return nullptr;
  }
}

}  // namespace

auto SensorManager::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  printf("[SensorManager] Initializing...\n");

  adc_init();
  adc_gpio_init(Config::SOIL_MOISTURE_PIN);

  auto* bmeI2C = resolveI2CInstance(Config::BME280_I2C_INSTANCE);
  auto* waterI2C =
    Config::WATER_LEVEL_I2C_ENABLED ? resolveI2CInstance(Config::WATER_LEVEL_I2C_INSTANCE) : bmeI2C;
  const bool waterInstanceInvalid = Config::WATER_LEVEL_I2C_ENABLED and (waterI2C == nullptr);
  if ((bmeI2C == nullptr) or waterInstanceInvalid)
  {
    printf("[SensorManager] ERROR: Unsupported I2C instance (BME=%u, Water=%u)\n",
           Config::BME280_I2C_INSTANCE, Config::WATER_LEVEL_I2C_INSTANCE);
    return false;
  }

  const bool     sharedBus = (bmeI2C == waterI2C);
  const uint32_t waterBaud = Config::WATER_LEVEL_I2C_BAUDRATE;
  const uint32_t bmeBaudRate =
    sharedBus ? std::min(Config::BME280_I2C_BAUDRATE, waterBaud) : Config::BME280_I2C_BAUDRATE;

  printf("[SensorManager] I2C config -> BME inst=%u, Water inst=%u (%s), shared=%s, BME baud=%lu, "
         "Water baud=%lu\n",
         Config::BME280_I2C_INSTANCE, Config::WATER_LEVEL_I2C_INSTANCE,
         Config::WATER_LEVEL_I2C_ENABLED ? "enabled" : "disabled", sharedBus ? "yes" : "no",
         static_cast<unsigned long>(bmeBaudRate), static_cast<unsigned long>(waterBaud));

  i2c_init(bmeI2C, bmeBaudRate);
  gpio_set_function(Config::BME280_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(Config::BME280_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(Config::BME280_SDA_PIN);
  gpio_pull_up(Config::BME280_SCL_PIN);

  if (Config::WATER_LEVEL_I2C_ENABLED and not sharedBus)
  {
    i2c_init(waterI2C, Config::WATER_LEVEL_I2C_BAUDRATE);
    gpio_set_function(Config::WATER_LEVEL_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(Config::WATER_LEVEL_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(Config::WATER_LEVEL_SDA_PIN);
    gpio_pull_up(Config::WATER_LEVEL_SCL_PIN);
  }

  if (Config::ENABLE_SERIAL_DEBUG)
  {
    printf("[SensorManager] Scanning I2C bus for devices...\n");
    scanI2CBus(bmeI2C, Config::BME280_I2C_INSTANCE);
    if (Config::WATER_LEVEL_I2C_ENABLED and not sharedBus)
    {
      scanI2CBus(waterI2C, Config::WATER_LEVEL_I2C_INSTANCE);
    }
  }

  bme280_.reset();
  uint8_t lastAddress = 0xFF;

  printf("[SensorManager] Probing BME280 at 0x%02X on I2C%u...\n", Config::BME280_I2C_ADDRESS,
         Config::BME280_I2C_INSTANCE);

  auto sensor = std::make_unique<BME280>(bmeI2C, Config::BME280_I2C_ADDRESS);
  if (sensor->init())
  {
    bme280_ = std::move(sensor);
    printf("[SensorManager] BME280 initialized successfully (addr=0x%02X)\n",
           Config::BME280_I2C_ADDRESS);
  }
  else
  {
    printf("[SensorManager] BME280 init failed at address 0x%02X\n", Config::BME280_I2C_ADDRESS);
  }

  if (not bme280_)
  {
    printf("[SensorManager] WARNING: BME280 not found or failed to initialize\n");
  }

  if (Config::WATER_LEVEL_I2C_ENABLED)
  {
    waterSensor_ = std::make_unique<WaterLevelSensor>(waterI2C);
    if (waterSensor_ and not waterSensor_->init())
    {
      printf("[SensorManager] WARNING: Water level sensor not detected\n");
      waterSensor_.reset();
    }
  }

  resistiveWaterSensor_ = std::make_unique<ResistiveWaterLevelSensor>();
  if (resistiveWaterSensor_ and not resistiveWaterSensor_->init())
  {
    printf("[SensorManager] WARNING: Resistive water level sensor init failed\n");
    resistiveWaterSensor_.reset();
  }

  initialized_ = true;
  printf("[SensorManager] Initialization complete\n");
  return true;
}

auto SensorManager::readAllSensors() -> SensorData
{
  auto data        = SensorData{};
  data.timestamp   = to_ms_since_boot(get_absolute_time());
  data.environment = readBME280();
  data.soil        = readSoilMoisture();
  if (Config::WATER_LEVEL_I2C_ENABLED)
  {
    data.water = readWaterLevel();
  }
  else
  {
    data.water = WaterLevelData{};
  }

  data.waterResistive = readResistiveWaterLevel();

  return data;
}

auto SensorManager::readBME280() -> BME280Data
{
  auto data = BME280Data{};

  if (not bme280_)
  {
    return data;
  }

  const auto measurement = bme280_->read();
  if (measurement)
  {
    data.temperature = measurement->temperature;
    data.humidity    = measurement->humidity;
    data.pressure    = measurement->pressure;
    data.valid       = true;
  }

  return data;
}

auto SensorManager::readResistiveWaterLevel() const -> ResistiveWaterData
{
  auto data = ResistiveWaterData{};

  if (not resistiveWaterSensor_)
  {
    cachedResistiveWaterData_ = {};
    return cachedResistiveWaterData_;
  }

  const uint32_t now           = to_ms_since_boot(get_absolute_time());
  const uint32_t refreshPeriod = Config::WATER_CHECK_INTERVAL_MS;
  if (cachedResistiveWaterData_.isValid() and refreshPeriod > 0 and
      (now - lastResistiveWaterReadMs_) < refreshPeriod)
  {
    return cachedResistiveWaterData_;
  }

  const auto reading = resistiveWaterSensor_->read();
  if (not reading)
  {
    if (cachedResistiveWaterData_.isValid())
    {
      return cachedResistiveWaterData_;
    }
    cachedResistiveWaterData_ = {};
    lastResistiveWaterReadMs_ = now;
    return cachedResistiveWaterData_;
  }

  data.rawValue = reading->rawValue;
  data.state    = (reading->rawValue <= Config::WATER_LEVEL_RESISTIVE_LOW_THRESHOLD)
                    ? ReservoirState::LOW
                    : ReservoirState::OK;
  data.valid    = true;

  cachedResistiveWaterData_ = data;
  lastResistiveWaterReadMs_ = now;
  return cachedResistiveWaterData_;
}

auto SensorManager::readSoilMoisture() const -> SoilMoistureData
{
  auto data       = SoilMoistureData{};
  data.rawValue   = readADC(Config::SOIL_MOISTURE_ADC);
  data.percentage = 100.0F - mapToPercentage(data.rawValue, soilWetValue_, soilDryValue_);
  data.percentage = std::clamp(data.percentage, 0.0F, 100.0F);
  data.valid      = true;

  return data;
}

auto SensorManager::readWaterLevel() const -> WaterLevelData
{
  auto data = WaterLevelData{};

  if (not Config::WATER_LEVEL_I2C_ENABLED)
  {
    return data;
  }

  if (not waterSensor_)
  {
    if (not waterSensorMissingLogged_)
    {
      printf("[SensorManager] Water level sensor unavailable (init failed)\n");
      waterSensorMissingLogged_ = true;
    }
    return data;
  }

  const auto reading = waterSensor_->read();
  if (not reading)
  {
    waterSensorMissingLogged_ = false;
    if (not waterSensorReadFailedLogged_)
    {
      printf("[SensorManager] Water level sensor read failed, keeping old data\n");
      waterSensorReadFailedLogged_ = true;
    }
    return data;
  }

  if (waterSensorMissingLogged_ or waterSensorReadFailedLogged_)
  {
    printf("[SensorManager] Water level sensor recovered (depth=%umm, sections=%u)\n",
           reading->depthMm, reading->sections);
  }
  waterSensorMissingLogged_    = false;
  waterSensorReadFailedLogged_ = false;

  data.rawValue   = reading->depthMm;
  data.percentage = reading->percentage;
  data.valid      = true;

  return data;
}

void SensorManager::calibrateSoilMoisture(const uint16_t dryValue, const uint16_t wetValue) noexcept
{
  soilDryValue_ = dryValue;
  soilWetValue_ = wetValue;
  printf("[SensorManager] Soil moisture calibrated: dry=%u, wet=%u\n", dryValue, wetValue);
}

auto SensorManager::readADC(const uint8_t channel) -> uint16_t
{
  adc_select_input(channel);
  return adc_read();
}

auto SensorManager::mapToPercentage(const uint16_t value, const uint16_t minVal,
                                    const uint16_t maxVal) noexcept -> float
{
  if (maxVal <= minVal)
  {
    return 0.0F;
  }

  if (value <= minVal)
  {
    return 0.0F;
  }

  if (value >= maxVal)
  {
    return 100.0F;
  }

  return (static_cast<float>(value - minVal) / static_cast<float>(maxVal - minVal)) * 100.0F;
}

void SensorManager::scanI2CBus(i2c_inst_t* const bus, const uint8_t instanceId)
{
  if (bus == nullptr)
  {
    printf("[SensorManager] I2C%u scan skipped (null instance)\n", instanceId);
    return;
  }

  constexpr uint8_t kStartAddress{0x00};
  constexpr uint8_t kEndAddress{0x7F};

  printf("[SensorManager] Scanning I2C%u bus for devices...\n", instanceId);
  uint8_t detected = 0;

  for (uint8_t address = kStartAddress; address < kEndAddress; ++address)
  {
    const int result = i2c_write_blocking(bus, address, nullptr, 0, false);
    if (result >= 0)
    {
      printf("  -> Device acknowledged at 0x%02X\n", address);
      ++detected;
    }
  }

  if (detected == 0)
  {
    printf("  (no I2C devices detected on I2C%u)\n", instanceId);
  }

  printf("[SensorManager] I2C%u scan complete (%u device%s found)\n", instanceId, detected,
         (detected == 1) ? "" : "s");
}
