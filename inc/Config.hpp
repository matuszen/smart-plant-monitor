#pragma once

#include "Types.hpp"

#include <pico/stdlib.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Config
{

namespace System
{
inline constexpr std::string_view VERSION    = "1.0";
inline constexpr std::string_view NAME       = "Smart Plant Monitor";
inline constexpr const char*      IDENTIFIER = "smart-plant-monitor";
}  // namespace System

namespace AP
{
inline constexpr const char* DEFAULT_SSID       = "PlantMonitor-Setup";
inline constexpr const char* DEFAULT_PASS       = "plantsetup";
inline constexpr uint32_t    SESSION_TIMEOUT_MS = 600'000;
}  // namespace AP

namespace WiFi
{
inline constexpr const char* HOSTNAME     = System::IDENTIFIER;
inline constexpr const char* DEFAULT_SSID = "";
inline constexpr const char* DEFAULT_PASS = "";
}  // namespace WiFi

namespace MQTT
{
inline constexpr const char* DEFAULT_BROKER_HOST         = "homeassistant.local";
inline constexpr uint16_t    DEFAULT_BROKER_PORT         = 1883;
inline constexpr const char* DEFAULT_CLIENT_ID           = System::IDENTIFIER;
inline constexpr const char* DEFAULT_USERNAME            = nullptr;
inline constexpr const char* DEFAULT_PASSWORD            = nullptr;
inline constexpr const char* DEFAULT_DISCOVERY_PREFIX    = "homeassistant";
inline constexpr const char* DEFAULT_BASE_TOPIC          = "smartplant";
inline constexpr uint32_t    DEFAULT_PUBLISH_INTERVAL_MS = 3'600'000;
inline constexpr uint32_t    RECONNECT_INTERVAL_MS       = 5'000;
}  // namespace MQTT

inline constexpr uint8_t  BME280_I2C_INSTANCE = 0;
inline constexpr uint8_t  BME280_SDA_PIN      = 4;
inline constexpr uint8_t  BME280_SCL_PIN      = 5;
inline constexpr uint32_t BME280_I2C_BAUDRATE = 400'000;
inline constexpr uint8_t  BME280_I2C_ADDRESS  = 0x76;

inline constexpr uint8_t  LIGHT_SENSOR_I2C_INSTANCE = BME280_I2C_INSTANCE;
inline constexpr uint8_t  LIGHT_SENSOR_SDA_PIN      = BME280_SDA_PIN;
inline constexpr uint8_t  LIGHT_SENSOR_SCL_PIN      = BME280_SCL_PIN;
inline constexpr uint32_t LIGHT_SENSOR_I2C_BAUDRATE = BME280_I2C_BAUDRATE;
inline constexpr uint8_t  LIGHT_SENSOR_I2C_ADDRESS  = 0x23;

inline constexpr uint8_t  SOIL_MOISTURE_POWER_UP_PIN  = 22;
inline constexpr uint32_t SOIL_MOISTURE_POWER_UP_MS   = 500;
inline constexpr uint8_t  SOIL_MOISTURE_ADC_PIN       = 26;
inline constexpr uint8_t  SOIL_MOISTURE_ADC_CHANNEL   = 0;
inline constexpr uint16_t SOIL_DRY_VALUE              = 3'500;
inline constexpr uint16_t SOIL_WET_VALUE              = 1'500;
inline constexpr float    SOIL_MOISTURE_DRY_THRESHOLD = 30.0F;
inline constexpr float    SOIL_MOISTURE_WET_THRESHOLD = 70.0F;

inline constexpr uint8_t  WATER_LEVEL_I2C_INSTANCE      = 1;
inline constexpr uint8_t  WATER_LEVEL_SDA_PIN           = 18;
inline constexpr uint8_t  WATER_LEVEL_SCL_PIN           = 19;
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
inline constexpr uint8_t  WATER_LEVEL_POWER_PIN         = 14;

inline constexpr uint8_t PUMP_CONTROL_PIN = 2;

inline constexpr uint8_t LED_STATUS_PIN  = 1;
inline constexpr uint8_t LED_NETWORK_PIN = 3;
inline constexpr uint8_t LED_ERROR_PIN   = 7;

inline constexpr uint8_t  BUTTON_PIN       = 0;
inline constexpr uint32_t BUTTON_AP_MIN_MS = 3'000;
inline constexpr uint32_t BUTTON_REBOOT_MS = 5'000;

inline constexpr uint32_t MIN_WATERING_DURATION_MS     = 1'000;
inline constexpr uint32_t MAX_WATERING_DURATION_MS     = 5'000;
inline constexpr uint32_t DEFAULT_WATERING_DURATION_MS = 3'000;
inline constexpr uint32_t WATERING_COOLDOWN_MS         = 300'000;
inline constexpr uint32_t IRRIGATION_ACTIVE_TICK_MS    = 200;

inline constexpr float    EVAPO_SOIL_BUCKET_MM        = 35.0F;
inline constexpr float    EVAPO_MIN_DROP_PER_HOUR_PCT = 0.05F;
inline constexpr uint32_t EVAPO_MAX_SLEEP_MS          = 900'000;

inline constexpr uint32_t INITIAL_DELAY_MS    = 5'000;
inline constexpr bool     ENABLE_SERIAL_DEBUG = true;
inline constexpr size_t   MAX_LOG_ENTRIES     = 1'000;
inline constexpr uint32_t SERIAL_BAUDRATE     = 115'200;

inline constexpr uint32_t       DEFAULT_SENSOR_READ_INTERVAL_MS = 3'600'000;
inline constexpr IrrigationMode DEFAULT_IRRIGATION_MODE         = IrrigationMode::EVAPOTRANSPIRATION;

}  // namespace Config
