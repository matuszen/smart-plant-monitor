#pragma once

#include <concepts>
#include <cstdint>

template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

template <typename T>
concept SensorValue = Numeric<T> and std::copyable<T>;

enum class IrrigationMode : uint8_t
{
  OFF       = 0,
  MANUAL    = 1,
  AUTOMATIC = 2,
  SCHEDULED = 3
};

enum class SystemStatus : uint8_t
{
  INITIALIZING = 0,
  READY        = 1,
  WATERING     = 2,
  ERROR        = 3,
  LOW_WATER    = 4
};

struct BME280Data
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

struct WaterLevelData
{
  float    percentage{0.0F};
  uint16_t rawValue{0};  // Represents estimated depth in millimeters.
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

enum class ReservoirState : uint8_t
{
  UNKNOWN = 0,
  OK      = 1,
  LOW     = 2
};

struct ResistiveWaterData
{
  ReservoirState state{ReservoirState::UNKNOWN};
  uint16_t       rawValue{0};
  bool           valid{false};

  [[nodiscard]] constexpr auto isValid() const noexcept -> bool
  {
    return valid;
  }
  [[nodiscard]] constexpr auto isLow() const noexcept -> bool
  {
    return valid and state == ReservoirState::LOW;
  }
  [[nodiscard]] constexpr auto isOk() const noexcept -> bool
  {
    return valid and state == ReservoirState::OK;
  }
};

struct SensorData
{
  BME280Data         environment;
  SoilMoistureData   soil;
  WaterLevelData     water;
  ResistiveWaterData waterResistive;
  uint32_t           timestamp{0};

  [[nodiscard]] constexpr auto allValid() const noexcept -> bool
  {
    return environment.isValid() and soil.isValid() and waterResistive.isValid();
  }
};

struct DataLogEntry
{
  SensorData data;
  bool       wasWatering{false};
  bool       uploaded{false};
  uint32_t   id{0};

  [[nodiscard]] constexpr auto needsUpload() const noexcept -> bool
  {
    return not uploaded and data.allValid();
  }
};
