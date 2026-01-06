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

  LightSensor(const LightSensor&)                    = delete;
  auto operator=(const LightSensor&) -> LightSensor& = delete;
  LightSensor(LightSensor&&)                         = delete;
  auto operator=(LightSensor&&) -> LightSensor&      = delete;

  auto init() -> bool;
  auto read() -> std::optional<LightLevelData>;
  auto isAvailable() const -> bool
  {
    return initialized_;
  }

private:
  static constexpr uint8_t POWER_ON_CMD           = 0x01;
  static constexpr uint8_t RESET_CMD              = 0x07;
  static constexpr uint8_t CONT_HIGH_RES_MODE_CMD = 0x10;

  i2c_inst_t* i2c_              = nullptr;
  uint8_t     address_          = 0;
  bool        initialized_      = false;
  bool        readFailedLogged_ = false;

  auto writeCommand(uint8_t command) -> bool;
};
