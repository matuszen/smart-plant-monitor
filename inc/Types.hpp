#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <type_traits>

template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

template <typename T>
concept SensorValue = Numeric<T> and std::copyable<T>;

struct WifiCredentials
{
  std::array<char, 33> ssid{};
  std::array<char, 65> pass{};
  bool                 valid{false};
};

struct MqttConfig
{
  std::array<char, 64> brokerHost{};
  uint16_t             brokerPort{1883};
  std::array<char, 32> clientId{};
  std::array<char, 32> username{};
  std::array<char, 32> password{};
  std::array<char, 32> discoveryPrefix{};
  std::array<char, 32> baseTopic{};
  uint32_t             publishIntervalMs{3'600'000};
  bool                 enabled{true};
};

struct ApConfig
{
  std::array<char, 33> ssid{};
  std::array<char, 65> pass{};
};

enum class IrrigationMode : uint8_t
{
  OFF                = 0,
  MANUAL             = 1,
  TIMER              = 2,
  HUMIDITY           = 3,
  EVAPOTRANSPIRATION = 4
};

struct SystemConfig
{
  WifiCredentials wifi{};
  ApConfig        ap{};
  MqttConfig      mqtt{};
  uint32_t        sensorReadIntervalMs{3'600'000};
  IrrigationMode  irrigationMode{IrrigationMode::TIMER};
};

enum class SystemStatus : uint8_t
{
  INITIALIZING = 0,
  READY        = 1,
  WATERING     = 2,
  ERROR        = 3,
  LOW_WATER    = 4
};

struct EnvironmentData
{
  float temperature{0.0F};
  float humidity{0.0F};
  float pressure{0.0F};
  bool  valid{false};

  [[nodiscard]] constexpr auto isValid() const noexcept -> bool
  {
    return valid;
  }
};

struct SoilMoistureData
{
  float    percentage{0.0F};
  uint16_t rawValue{0};
  bool     valid{false};

  [[nodiscard]] constexpr auto isValid() const noexcept -> bool
  {
    return valid;
  }
  [[nodiscard]] constexpr auto isDry() const noexcept -> bool
  {
    return valid and percentage < 30.0F;
  }
  [[nodiscard]] constexpr auto isWet() const noexcept -> bool
  {
    return valid and percentage > 70.0F;
  }
};

struct LightLevelData
{
  uint16_t rawValue{0};
  float    lux{0.0F};
  bool     valid{false};

  [[nodiscard]] constexpr auto isValid() const noexcept -> bool
  {
    return valid;
  }
};

struct WaterLevelData
{
  float    percentage{0.0F};
  uint16_t activeSections{0};
  bool     valid{false};

  [[nodiscard]] constexpr auto isValid() const noexcept -> bool
  {
    return valid;
  }
  [[nodiscard]] constexpr auto isEmpty() const noexcept -> bool
  {
    return valid and percentage < 10.0F;
  }
  [[nodiscard]] constexpr auto isLow() const noexcept -> bool
  {
    return valid and percentage < 25.0F;
  }
  [[nodiscard]] constexpr auto isFull() const noexcept -> bool
  {
    return valid and percentage > 80.0F;
  }
};

struct SensorData
{
  EnvironmentData  environment;
  LightLevelData   light;
  SoilMoistureData soil;
  WaterLevelData   water;
  uint32_t         timestamp{0};

  [[nodiscard]] constexpr auto allValid() const noexcept -> bool
  {
    return environment.isValid() and soil.isValid() and light.isValid() and water.isValid();
  }
};
