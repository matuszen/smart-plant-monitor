#include "FlashManager.hpp"
#include "Types.hpp"

#include <FreeRTOS.h>
#include <boards/pico2_w.h>
#include <cyw43_configport.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <pico/cyw43_arch.h>
#include <pico/error.h>
#include <pico/flash.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <pico/platform/sections.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <task.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace
{

inline constexpr uint32_t CONFIG_MAGIC      = 0x53'59'53'43U;  // SYSC
inline constexpr uint32_t CRC32_POLYNOMIAL  = 0xED'B8'83'20U;
inline constexpr uint32_t CRC32_INITIAL     = 0xFF'FF'FF'FFU;
inline constexpr uint32_t SAVING_TIMEOUT_MS = 2'000U;

[[nodiscard]] constexpr auto crc32(const void* const data, const size_t len) -> uint32_t
{
  auto        crc      = CRC32_INITIAL;
  const auto* bytes    = static_cast<const uint8_t*>(data);
  const auto  byteSpan = std::span<const uint8_t>(bytes, len);
  for (const auto byte : byteSpan)
  {
    crc ^= byte;
    for (int32_t j = 0; j < 8; ++j)
    {
      const auto mask = -(crc & 1U);
      crc             = (crc >> 1U) ^ (CRC32_POLYNOMIAL & mask);
    }
  }
  return ~crc;
}

[[nodiscard]] constexpr auto getOffsetSize() -> uint32_t
{
  return PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
}

void __no_inline_not_in_flash_func(flashProgramTrampoline)(void* const param)
{
  const auto* ctx = static_cast<FlashOpContext*>(param);
  flash_range_program(ctx->offset, ctx->data, ctx->size);
}

void __no_inline_not_in_flash_func(flashEraseTrampoline)(void* const param)
{
  const auto* ctx = static_cast<FlashOpContext*>(param);
  flash_range_erase(ctx->offset, ctx->size);
}

}  // namespace

auto FlashManager::getInstance() -> FlashManager&
{
  static FlashManager instance;
  return instance;
}

auto FlashManager::getFlashAddress(const uint32_t offset) -> uint32_t
{
  return XIP_BASE + offset;
}

auto FlashManager::read(const uint32_t offset, const std::span<uint8_t> buffer) -> bool
{
  if (offset + buffer.size() > PICO_FLASH_SIZE_BYTES)
  {
    return false;
  }

  const auto* flashPtr = std::bit_cast<const uint8_t*>(getFlashAddress(offset));
  std::memcpy(buffer.data(), flashPtr, buffer.size());
  return true;
}

auto FlashManager::write(const uint32_t offset, const std::span<const uint8_t> data) -> bool
{
  if (offset + data.size() > PICO_FLASH_SIZE_BYTES)
  {
    return false;
  }

  auto ctx = FlashOpContext{
    .offset = offset,
    .data   = data.data(),
    .size   = data.size(),
  };

  if (not flushOutputBuffers())
  {
    return false;
  }

  cyw43_arch_lwip_begin();
  vTaskSuspendAll();

  const auto interrupts = save_and_disable_interrupts();
  flashProgramTrampoline(&ctx);
  restore_interrupts(interrupts);

  xTaskResumeAll();
  cyw43_arch_lwip_end();

  printf("[FlashManager] Write succeeded\n");

  return true;
}

auto FlashManager::erase(const uint32_t offset, const size_t size) -> bool
{
  if (offset + size > PICO_FLASH_SIZE_BYTES)
  {
    printf("[FlashManager] Erase range out of bounds\n");
    return false;
  }
  if ((offset % FLASH_SECTOR_SIZE != 0) or (size % FLASH_SECTOR_SIZE != 0))
  {
    printf("[FlashManager] Erase alignment error\n");
    return false;
  }

  auto ctx = FlashOpContext{
    .offset = offset,
    .data   = nullptr,
    .size   = size,
  };

  if (not flushOutputBuffers())
  {
    return false;
  }

  cyw43_arch_lwip_begin();
  vTaskSuspendAll();

  const auto interrupts = save_and_disable_interrupts();
  flashEraseTrampoline(&ctx);
  restore_interrupts(interrupts);

  xTaskResumeAll();
  cyw43_arch_lwip_end();

  printf("[FlashManager] Erase succeeded\n");

  return true;
}

auto FlashManager::loadConfig(SystemConfig& config) -> bool
{
  const auto offset = getOffsetSize();
  auto       record = FlashRecord{};

  if (not read(offset, std::span(reinterpret_cast<uint8_t*>(&record), sizeof(record))))
  {
    return false;
  }

  if (record.magic != CONFIG_MAGIC)
  {
    return false;
  }

  const uint32_t calculatedCrc = crc32(&record.config, sizeof(record.config));
  if (calculatedCrc != record.crc)
  {
    return false;
  }

  config = record.config;
  return true;
}

auto FlashManager::saveConfig(const SystemConfig& config) -> bool
{
  const auto offset = getOffsetSize();
  const auto record = FlashRecord{
    .magic  = CONFIG_MAGIC,
    .config = config,
    .crc    = crc32(&config, sizeof(config)),
  };

  if (not erase(offset, FLASH_SECTOR_SIZE))
  {
    return false;
  }

  return write(offset, std::span(reinterpret_cast<const uint8_t*>(&record), sizeof(record)));
}

auto FlashManager::flushOutputBuffers() -> bool
{
  if (fflush(stdout) != 0)
  {
    printf("[FlashManager] Failed to flush stdout\n");
    return false;
  }
  if (fflush(stderr) != 0)
  {
    printf("[FlashManager] Failed to flush stderr\n");
    return false;
  }
  return true;
}
