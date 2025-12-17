#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

template <typename T>
concept SensorValue = Numeric<T> and std::copyable<T>;

enum class IrrigationMode : uint8_t
{
  OFF                = 0,
  MANUAL             = 1,
  TIMER              = 2,
  HUMIDITY           = 3,
  EVAPOTRANSPIRATION = 4
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

struct HallSensorData
{
  bool magnetDetected{false};
  bool valid{false};

  [[nodiscard]] constexpr auto isValid() const noexcept -> bool
  {
    return valid;
  }
  [[nodiscard]] constexpr auto isMagnetPresent() const noexcept -> bool
  {
    return valid and magnetDetected;
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
  HallSensorData   hall;
  uint32_t         timestamp{0};

  [[nodiscard]] constexpr auto allValid() const noexcept -> bool
  {
    return environment.isValid() and soil.isValid() and light.isValid() and water.isValid() and hall.isValid();
  }
};
