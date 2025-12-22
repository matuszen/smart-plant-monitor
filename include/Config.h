#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <pico/stdlib.h>

namespace Config
{

inline constexpr std::string_view SYSTEM_VERSION    = "1.0";
inline constexpr std::string_view SYSTEM_NAME       = "Smart Plant Monitor";
inline constexpr const char*      DEVICE_IDENTIFIER = "smart-plant-monitor";

inline constexpr bool        ENABLE_HOME_ASSISTANT    = false;
inline constexpr const char* WIFI_SSID                = "NetworkName";
inline constexpr const char* WIFI_PASSWORD            = "PASSWD1234";
inline constexpr const char* MQTT_BROKER_HOST         = "192.168.1.10";
inline constexpr uint16_t    MQTT_BROKER_PORT         = 1883;
inline constexpr const char* MQTT_CLIENT_ID           = DEVICE_IDENTIFIER;
inline constexpr const char* MQTT_USERNAME            = nullptr;
inline constexpr const char* MQTT_PASSWORD            = nullptr;
inline constexpr const char* HA_DISCOVERY_PREFIX      = "homeassistant";
inline constexpr const char* HA_BASE_TOPIC            = "smartplant";
inline constexpr uint32_t    HA_PUBLISH_INTERVAL_MS   = 15'000;
inline constexpr uint32_t    HA_RECONNECT_INTERVAL_MS = 5'000;

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
inline constexpr uint16_t WATER_LEVEL_MAX_DEPTH_MM      = WATER_LEVEL_SECTION_HEIGHT_MM * WATER_LEVEL_TOTAL_SECTIONS;

inline constexpr uint8_t WATER_LEVEL_POWER_PIN = 14;
inline constexpr uint8_t PUMP_CONTROL_PIN      = 2;
inline constexpr bool    RELAY_ACTIVE_HIGH     = true;

inline constexpr uint8_t BUTTON_PIN      = 0;
inline constexpr uint8_t LED_STATUS_PIN  = 1;
inline constexpr uint8_t LED_NETWORK_PIN = 3;
inline constexpr uint8_t LED_ERROR_PIN   = 7;

inline constexpr uint32_t BUTTON_AP_MIN_MS      = 5'000;
inline constexpr uint32_t BUTTON_REBOOT_MS      = 10'000;
inline constexpr uint32_t AP_SESSION_TIMEOUT_MS = 600'000;

inline constexpr uint32_t MIN_WATERING_DURATION_MS     = 1'000;
inline constexpr uint32_t MAX_WATERING_DURATION_MS     = 5'000;
inline constexpr uint32_t DEFAULT_WATERING_DURATION_MS = 3'000;
inline constexpr uint32_t WATERING_COOLDOWN_MS         = 300'000;
inline constexpr uint32_t IRRIGATION_ACTIVE_TICK_MS    = 200;

constexpr const char* AP_SSID = "PlantMonitor-Setup";
constexpr const char* AP_PASS = "plantsetup";

inline constexpr float    EVAPO_SOIL_BUCKET_MM        = 35.0F;
inline constexpr float    EVAPO_MIN_DROP_PER_HOUR_PCT = 0.05F;
inline constexpr uint32_t EVAPO_MAX_SLEEP_MS          = 900'000;

inline constexpr bool     ENABLE_SERIAL_DEBUG = true;
inline constexpr size_t   MAX_LOG_ENTRIES     = 1'000;
inline constexpr uint32_t SERIAL_BAUDRATE     = 115'200;

inline constexpr uint32_t SENSOR_READ_INTERVAL_MS = ENABLE_SERIAL_DEBUG ? 10'000 : 3'600'000;
inline constexpr uint32_t BME280_READ_INTERVAL_MS = 10'000;
inline constexpr uint32_t DATA_SEND_INTERVAL_MS   = 60'000;

}  // namespace Config
