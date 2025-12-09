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
#include "LightSensor.h"
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
    case 1:
      return i2c1;
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
  adc_gpio_init(Config::SOIL_MOISTURE_ADC_PIN);
  gpio_init(Config::SOIL_MOISTURE_POWER_UP_PIN);
  gpio_set_dir(Config::SOIL_MOISTURE_POWER_UP_PIN, GPIO_OUT);
  gpio_put(Config::SOIL_MOISTURE_POWER_UP_PIN, false);

  auto* bmeI2C   = resolveI2CInstance(Config::BME280_I2C_INSTANCE);
  auto* lightI2C = resolveI2CInstance(Config::LIGHT_SENSOR_I2C_INSTANCE);
  auto* waterI2C = resolveI2CInstance(Config::WATER_LEVEL_I2C_INSTANCE);

  const bool waterInstanceInvalid  = waterI2C == nullptr;
  const bool bme280InstanceInvalid = bmeI2C == nullptr;
  const bool lightInstanceInvalid  = lightI2C == nullptr;

  [[unlikely]]
  if (bme280InstanceInvalid or waterInstanceInvalid or lightInstanceInvalid)
  {
    printf("[SensorManager] ERROR: Unsupported I2C instance (BME=%u, Light=%u, Water=%u)\n",
           Config::BME280_I2C_INSTANCE, Config::LIGHT_SENSOR_I2C_INSTANCE, Config::WATER_LEVEL_I2C_INSTANCE);
    return false;
  }

  const bool bmeLightShared = Config::BME280_I2C_INSTANCE == Config::LIGHT_SENSOR_I2C_INSTANCE;

  printf("[SensorManager] I2C config -> BME inst=%u, Light inst=%u, Water inst=%u, BME/Light shared=%s, "
         "BME baud=%lu, Light baud=%lu, Water baud=%lu\n",
         Config::BME280_I2C_INSTANCE, Config::LIGHT_SENSOR_I2C_INSTANCE, Config::WATER_LEVEL_I2C_INSTANCE,
         bmeLightShared ? "yes" : "no", static_cast<unsigned long>(Config::BME280_I2C_BAUDRATE),
         static_cast<unsigned long>(Config::LIGHT_SENSOR_I2C_BAUDRATE),
         static_cast<unsigned long>(Config::WATER_LEVEL_I2C_BAUDRATE));

  i2c_init(bmeI2C, Config::BME280_I2C_BAUDRATE);
  gpio_set_function(Config::BME280_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(Config::BME280_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(Config::BME280_SDA_PIN);
  gpio_pull_up(Config::BME280_SCL_PIN);

  if (not bmeLightShared)
  {
    i2c_init(lightI2C, Config::LIGHT_SENSOR_I2C_BAUDRATE);
    gpio_set_function(Config::LIGHT_SENSOR_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(Config::LIGHT_SENSOR_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(Config::LIGHT_SENSOR_SDA_PIN);
    gpio_pull_up(Config::LIGHT_SENSOR_SCL_PIN);
  }

  i2c_init(waterI2C, Config::WATER_LEVEL_I2C_BAUDRATE);
  gpio_set_function(Config::WATER_LEVEL_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(Config::WATER_LEVEL_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(Config::WATER_LEVEL_SDA_PIN);
  gpio_pull_up(Config::WATER_LEVEL_SCL_PIN);

  // if constexpr (Config::ENABLE_SERIAL_DEBUG)
  // {
  //   printf("[SensorManager] Scanning I2C bus for devices...\n");
  //   scanI2CBus(bmeI2C, Config::BME280_I2C_INSTANCE);
  //   if (not sharedBus)
  //   {
  //     scanI2CBus(waterI2C, Config::WATER_LEVEL_I2C_INSTANCE);
  //   }
  // }

  // bme280_.reset();

  // printf("[SensorManager] Probing BME280 at 0x%02X on I2C%u...\n", Config::BME280_I2C_ADDRESS,
  //        Config::BME280_I2C_INSTANCE);

  // auto sensor = std::make_unique<BME280>(bmeI2C, Config::BME280_I2C_ADDRESS);
  // if (sensor->init())
  // {
  //   bme280_ = std::move(sensor);
  //   printf("[SensorManager] BME280 initialized successfully (addr=0x%02X)\n", Config::BME280_I2C_ADDRESS);
  // }
  // else
  // {
  //   printf("[SensorManager] BME280 init failed at address 0x%02X\n", Config::BME280_I2C_ADDRESS);
  // }

  // if (not bme280_)
  // {
  //   printf("[SensorManager] WARNING: BME280 not found or failed to initialize\n");
  // }

  printf("[SensorManager] Probing BH1750 at 0x%02X on I2C%u...\n", Config::LIGHT_SENSOR_I2C_ADDRESS,
         Config::LIGHT_SENSOR_I2C_INSTANCE);
  lightSensor_ = std::make_unique<LightSensor>(lightI2C, Config::LIGHT_SENSOR_I2C_ADDRESS);
  if (lightSensor_ and not lightSensor_->init())
  {
    printf("[SensorManager] WARNING: BH1750 not detected\n");
    lightSensor_.reset();
  }

  waterSensor_ = std::make_unique<WaterLevelSensor>(waterI2C);
  if (waterSensor_ and not waterSensor_->init())
  {
    printf("[SensorManager] WARNING: Water level sensor not detected\n");
    waterSensor_.reset();
  }

  initialized_ = true;
  printf("[SensorManager] Initialization complete\n");
  return true;
}

auto SensorManager::readAllSensors() -> SensorData
{
  auto data = SensorData{
    .environment         = readBME280(),
    .light               = readLightLevel(),
    .soil                = readSoilMoisture(),
    .water               = readWaterLevel(),
    .lightLevelAvailable = lightSensor_ != nullptr,
    .waterLevelAvailable = waterSensor_ != nullptr,
    .timestamp           = to_ms_since_boot(get_absolute_time()),
  };
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

auto SensorManager::readLightLevel() const -> LightLevelData
{
  auto data = LightLevelData{};

  if (not lightSensor_)
  {
    if (not lightSensorMissingLogged_)
    {
      printf("[SensorManager] Light sensor unavailable (init failed)\n");
      lightSensorMissingLogged_ = true;
    }
    return data;
  }

  const auto reading = lightSensor_->read();
  if (not reading)
  {
    lightSensorMissingLogged_ = false;
    if (not lightSensorReadFailedLogged_)
    {
      printf("[SensorManager] Light sensor read failed\n");
      lightSensorReadFailedLogged_ = true;
    }
    return data;
  }

  if (lightSensorMissingLogged_ or lightSensorReadFailedLogged_)
  {
    printf("[SensorManager] Light sensor recovered (lux=%.1f)\n", reading->lux);
  }
  lightSensorMissingLogged_    = false;
  lightSensorReadFailedLogged_ = false;

  data.rawValue = reading->raw;
  data.lux      = reading->lux;
  data.valid    = true;
  return data;
}

auto SensorManager::readSoilMoisture() const -> SoilMoistureData
{
  auto data = SoilMoistureData{};
  gpio_put(Config::SOIL_MOISTURE_POWER_UP_PIN, true);
  sleep_ms(Config::SOIL_MOISTURE_POWER_UP_MS);
  data.rawValue = readADC(Config::SOIL_MOISTURE_ADC_CHANNEL);
  gpio_put(Config::SOIL_MOISTURE_POWER_UP_PIN, false);
  data.percentage = 100.0F - mapToPercentage(data.rawValue, soilWetValue_, soilDryValue_);
  data.percentage = std::clamp(data.percentage, 0.0F, 100.0F);
  data.valid      = true;

  return data;
}

auto SensorManager::readWaterLevel() const -> WaterLevelData
{
  auto data = WaterLevelData{};

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
    printf("[SensorManager] Water level sensor recovered (depth=%umm, sections=%u)\n", reading->depthMm,
           reading->sections);
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

constexpr void SensorManager::scanI2CBus(i2c_inst_t* const bus, const uint8_t instanceId)
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

  printf("[SensorManager] I2C%u scan complete (%u device%s found)\n", instanceId, detected, (detected == 1) ? "" : "s");
}
