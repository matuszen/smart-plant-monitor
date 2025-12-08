#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <pico/stdlib.h>

namespace Config
{

inline constexpr std::string_view SYSTEM_VERSION = "1.0";
inline constexpr std::string_view SYSTEM_NAME    = "Smart Plant Monitor";

inline constexpr uint8_t  BME280_I2C_INSTANCE = 0;
inline constexpr uint8_t  BME280_SDA_PIN      = 0;
inline constexpr uint8_t  BME280_SCL_PIN      = 1;
inline constexpr uint32_t BME280_I2C_BAUDRATE = 400'000;
inline constexpr uint8_t  BME280_I2C_ADDRESS  = 0x76;

inline constexpr uint8_t  SOIL_MOISTURE_POWER_UP_PIN  = 22;
inline constexpr uint32_t SOIL_MOISTURE_POWER_UP_MS   = 500;
inline constexpr uint8_t  SOIL_MOISTURE_ADC_PIN       = 26;
inline constexpr uint8_t  SOIL_MOISTURE_ADC_CHANNEL   = 0;
inline constexpr uint16_t SOIL_DRY_VALUE              = 3'500;
inline constexpr uint16_t SOIL_WET_VALUE              = 1'500;
inline constexpr float    SOIL_MOISTURE_DRY_THRESHOLD = 30.0F;
inline constexpr float    SOIL_MOISTURE_WET_THRESHOLD = 70.0F;

inline constexpr uint8_t  WATER_LEVEL_I2C_INSTANCE      = 0;
inline constexpr uint8_t  WATER_LEVEL_SDA_PIN           = 0;
inline constexpr uint8_t  WATER_LEVEL_SCL_PIN           = 1;
inline constexpr uint32_t WATER_LEVEL_I2C_BAUDRATE      = 100'000;
inline constexpr uint8_t  WATER_LEVEL_LOW_ADDR          = 0x77;
inline constexpr uint8_t  WATER_LEVEL_HIGH_ADDR         = 0x78;
inline constexpr uint8_t  WATER_LEVEL_TOUCH_THRESHOLD   = 100;
inline constexpr uint8_t  WATER_LEVEL_SECTION_HEIGHT_MM = 5;
inline constexpr uint8_t  WATER_LEVEL_TOTAL_SECTIONS    = 20;
inline constexpr uint8_t  WATER_LEVEL_READ_DELAY_MS     = 10;
inline constexpr uint8_t  WATER_LEVEL_WAKE_RETRIES      = 8;
inline constexpr uint16_t WATER_LEVEL_WAKE_DELAY_MS     = 50;
inline constexpr uint8_t  WATER_LEVEL_WAKE_MIN_SIGNAL   = 5;
inline constexpr uint16_t WATER_LEVEL_MAX_DEPTH_MM =
  WATER_LEVEL_SECTION_HEIGHT_MM * WATER_LEVEL_TOTAL_SECTIONS;

inline constexpr uint8_t RELAY_PIN         = 15;
inline constexpr bool    RELAY_ACTIVE_HIGH = true;

inline constexpr uint32_t MIN_WATERING_DURATION_MS     = 1'000;
inline constexpr uint32_t MAX_WATERING_DURATION_MS     = 300'000;
inline constexpr uint32_t DEFAULT_WATERING_DURATION_MS = 5'000;
inline constexpr uint32_t WATERING_COOLDOWN_MS         = 300'000;

inline constexpr uint8_t  STATUS_LED_PIN      = 16;
inline constexpr uint32_t STATUS_LED_BLINK_MS = 1'000;

inline constexpr uint32_t SENSOR_READ_INTERVAL_MS = 2'000;
inline constexpr uint32_t BME280_READ_INTERVAL_MS = 10'000;
inline constexpr uint32_t DATA_SEND_INTERVAL_MS   = 60'000;

inline constexpr bool     ENABLE_SERIAL_DEBUG = true;
inline constexpr size_t   MAX_LOG_ENTRIES     = 1'000;
inline constexpr uint32_t SERIAL_BAUDRATE     = 115'200;

}  // namespace Config
