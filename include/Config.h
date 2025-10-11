#pragma once

#include <cstdint>
#include <string_view>

namespace Config
{

inline constexpr std::string_view SYSTEM_VERSION{"1.0"};
inline constexpr std::string_view SYSTEM_NAME{"Smart Plant Monitor"};

inline constexpr uint8_t  BME280_I2C_INSTANCE{0};
inline constexpr uint8_t  BME280_SDA_PIN{0};
inline constexpr uint8_t  BME280_SCL_PIN{1};
inline constexpr uint32_t BME280_I2C_BAUDRATE{400'000};
inline constexpr uint8_t  BME280_I2C_ADDRESS{0x76};

inline constexpr uint8_t SOIL_MOISTURE_PIN{26};
inline constexpr uint8_t SOIL_MOISTURE_ADC{0};

inline constexpr uint16_t SOIL_DRY_VALUE{3500};
inline constexpr uint16_t SOIL_WET_VALUE{1500};

inline constexpr float SOIL_MOISTURE_DRY_THRESHOLD{30.0F};
inline constexpr float SOIL_MOISTURE_WET_THRESHOLD{70.0F};

inline constexpr uint8_t WATER_LEVEL_POWER_PIN{17};
inline constexpr uint8_t WATER_LEVEL_SIGNAL_PIN{27};
inline constexpr uint8_t WATER_LEVEL_ADC{1};

inline constexpr uint16_t WATER_EMPTY_THRESHOLD{500};
inline constexpr uint16_t WATER_LOW_THRESHOLD{1000};
inline constexpr uint16_t WATER_FULL_THRESHOLD{3000};

inline constexpr uint32_t WATER_SENSOR_POWER_DELAY_MS{100};

inline constexpr uint8_t RELAY_PIN{15};
inline constexpr bool    RELAY_ACTIVE_HIGH{true};

inline constexpr uint32_t MIN_WATERING_DURATION_MS{1'000};
inline constexpr uint32_t MAX_WATERING_DURATION_MS{300'000};
inline constexpr uint32_t DEFAULT_WATERING_DURATION_MS{5'000};
inline constexpr uint32_t WATERING_COOLDOWN_MS{300'000};

inline constexpr uint8_t STATUS_LED_PIN{25};

inline constexpr uint32_t SENSOR_READ_INTERVAL_MS{2'000};
inline constexpr uint32_t BME280_READ_INTERVAL_MS{10'000};
inline constexpr uint32_t WATER_CHECK_INTERVAL_MS{60'000};
inline constexpr uint32_t DATA_SEND_INTERVAL_MS{60'000};
inline constexpr uint32_t STATUS_LED_BLINK_MS{1'000};

inline constexpr size_t MAX_LOG_ENTRIES{1000};
inline constexpr bool   ENABLE_FLASH_LOGGING{false};

inline constexpr bool     ENABLE_SERIAL_DEBUG{true};
inline constexpr uint32_t SERIAL_BAUD_RATE{115200};

}  // namespace Config
