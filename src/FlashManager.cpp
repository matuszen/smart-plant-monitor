#include "FlashManager.hpp"
#include "Types.hpp"

#include <FreeRTOS.h>
#include <boards/pico2_w.h>
#include <hardware/flash.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <pico/error.h>
#include <pico/flash.h>
#include <pico/platform.h>
#include <pico/platform/sections.h>
#include <task.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace
{

inline constexpr uint32_t CONFIG_MAGIC = 0x53'59'53'43;  // SYSC

struct FlashRecord
{
  uint32_t     magic  = 0;
  SystemConfig config = {};
  uint32_t     crc    = 0;
};

[[nodiscard]] auto crc32(const void* data, const size_t len) -> uint32_t
{
  const auto*              bytes = static_cast<const uint8_t*>(data);
  std::span<const uint8_t> s(bytes, len);
  uint32_t                 crc = 0xFFFFFFFF;
  for (const auto byte : s)
  {
    crc ^= byte;
    for (int j = 0; j < 8; ++j)
    {
      const auto mask = -(crc & 1U);
      crc             = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

struct FlashOpContext
{
  uint32_t       offset;
  const uint8_t* data;
  size_t         size;
};

void __no_inline_not_in_flash_func(flashProgramTrampoline)(void* param)
{
  const auto* ctx = static_cast<FlashOpContext*>(param);
  flash_range_program(ctx->offset, ctx->data, ctx->size);
}

void __no_inline_not_in_flash_func(flashEraseTrampoline)(void* param)
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

auto FlashManager::readData(const uint32_t offset, const std::span<uint8_t> buffer) -> bool
{
  if (offset + buffer.size() > PICO_FLASH_SIZE_BYTES)
  {
    return false;
  }

  const auto* flashPtr = std::bit_cast<const uint8_t*>(getFlashAddress(offset));
  std::memcpy(buffer.data(), flashPtr, buffer.size());
  return true;
}

auto FlashManager::writeData(const uint32_t offset, const std::span<const uint8_t> data) -> bool
{
  if (offset + data.size() > PICO_FLASH_SIZE_BYTES)
  {
    return false;
  }

  FlashOpContext ctx{
    .offset = offset,
    .data   = data.data(),
    .size   = data.size(),
  };

  printf("[FlashManager] Write: calling flash_safe_execute\n");

  taskENTER_CRITICAL();
  const auto result = flash_safe_execute(flashProgramTrampoline, &ctx, 1000);
  taskEXIT_CRITICAL();

  printf("[FlashManager] Write: returned %d\n", result);

  if (result != PICO_OK)
  {
    printf("[FlashManager] Write failed: %d\n", result);
    return false;
  }

  return true;
}

auto FlashManager::erase(const uint32_t offset, const size_t size) -> bool
{
  if (offset + size > PICO_FLASH_SIZE_BYTES)
  {
    return false;
  }

  if ((offset % FLASH_SECTOR_SIZE != 0) or (size % FLASH_SECTOR_SIZE != 0))
  {
    printf("[FlashManager] Erase alignment error\n");
    return false;
  }

  FlashOpContext ctx{
    .offset = offset,
    .data   = nullptr,
    .size   = size,
  };

  printf("[FlashManager] Erase: calling flash_safe_execute\n");
  if (fflush(stdout) != 0)
  {
    printf("[FlashManager] Failed to flush stdout\n");
  }
  vTaskSuspendAll();
  const auto result = flash_safe_execute(flashEraseTrampoline, &ctx, 2000);
  xTaskResumeAll();
  printf("[FlashManager] Erase: returned %d\n", result);
  if (fflush(stdout) != 0)
  {
    printf("[FlashManager] Failed to flush stdout\n");
  }

  if (result != PICO_OK)
  {
    printf("[FlashManager] Erase failed: %d\n", result);
    return false;
  }

  return true;
}

auto FlashManager::loadConfig(SystemConfig& config) -> bool
{
  const uint32_t offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
  FlashRecord    record;
  if (!readData(offset, std::span(reinterpret_cast<uint8_t*>(&record), sizeof(record))))
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
  const uint32_t offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
  FlashRecord    record;
  record.magic  = CONFIG_MAGIC;
  record.config = config;
  record.crc    = crc32(&record.config, sizeof(record.config));

  if (!erase(offset, FLASH_SECTOR_SIZE))
  {
    return false;
  }

  return writeData(offset, std::span(reinterpret_cast<const uint8_t*>(&record), sizeof(record)));
}
