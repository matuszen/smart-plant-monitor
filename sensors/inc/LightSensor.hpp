#pragma once

#include "Config.hpp"
#include "Types.hpp"

#include <hardware/i2c.h>

#include <cstdint>
#include <optional>

class LightSensor final
{
public:
  explicit LightSensor(i2c_inst_t* i2c, uint8_t address = Config::LIGHT_SENSOR_I2C_ADDRESS);
  ~LightSensor() = default;

  LightSensor(const LightSensor&)                        = delete;
  auto operator=(const LightSensor&) -> LightSensor&     = delete;
  LightSensor(LightSensor&&) noexcept                    = delete;
  auto operator=(LightSensor&&) noexcept -> LightSensor& = delete;

  [[nodiscard]] auto init() -> bool;
  [[nodiscard]] auto read() -> std::optional<LightLevelData>;
  [[nodiscard]] auto isAvailable() const noexcept -> bool
  {
    return initialized_;
  }

private:
  static constexpr uint8_t POWER_ON_CMD{0x01};
  static constexpr uint8_t RESET_CMD{0x07};
  static constexpr uint8_t CONT_HIGH_RES_MODE_CMD{0x10};

  i2c_inst_t* i2c_{};
  uint8_t     address_{};
  bool        initialized_{false};
  bool        readFailedLogged_{false};

  [[nodiscard]] auto writeCommand(uint8_t command) -> bool;
};
