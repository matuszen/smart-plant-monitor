#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class FlashManager
{
public:
  static auto getInstance() -> FlashManager&;

  [[nodiscard]] auto readData(uint32_t offset, std::span<uint8_t> buffer) -> bool;
  [[nodiscard]] auto writeData(uint32_t offset, std::span<const uint8_t> data) -> bool;
  [[nodiscard]] auto erase(uint32_t offset, size_t size) -> bool;

  // Helper to get absolute flash address from offset
  [[nodiscard]] static auto getFlashAddress(uint32_t offset) -> uint32_t;

private:
  FlashManager()  = default;
  ~FlashManager() = default;

  FlashManager(const FlashManager&)                    = delete;
  auto operator=(const FlashManager&) -> FlashManager& = delete;
  FlashManager(FlashManager&&)                         = delete;
  auto operator=(FlashManager&&) -> FlashManager&      = delete;
};
