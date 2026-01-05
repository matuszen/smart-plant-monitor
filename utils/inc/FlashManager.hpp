#pragma once

#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

struct FlashRecord
{
  uint32_t     magic  = 0;
  SystemConfig config = {};
  uint32_t     crc    = 0;
};

struct FlashOpContext
{
  uint32_t       offset = 0;
  const uint8_t* data   = nullptr;
  size_t         size   = 0;
};

class FlashManager final
{
public:
  FlashManager(const FlashManager&)                    = delete;
  auto operator=(const FlashManager&) -> FlashManager& = delete;
  FlashManager(FlashManager&&)                         = delete;
  auto operator=(FlashManager&&) -> FlashManager&      = delete;

  static auto getInstance() -> FlashManager&;

  [[nodiscard]] static auto read(uint32_t offset, std::span<uint8_t> buffer) -> bool;
  [[nodiscard]] static auto write(uint32_t offset, std::span<const uint8_t> data) -> bool;
  [[nodiscard]] static auto erase(uint32_t offset, size_t size) -> bool;

  [[nodiscard]] static auto getFlashAddress(uint32_t offset) -> uint32_t;

  static auto loadConfig(SystemConfig& config) -> bool;
  static auto saveConfig(const SystemConfig& config) -> bool;

  [[nodiscard]] static auto flushOutputBuffers() -> bool;

private:
  FlashManager()  = default;
  ~FlashManager() = default;
};
