#include "FlashManager.h"

#include <cstdio>
#include <cstring>

#include <FreeRTOS.h>
#include <task.h>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/flash.h>
#include <pico/platform.h>

namespace
{

struct FlashOpContext
{
  uint32_t       offset;
  const uint8_t* data;
  size_t         size;
};

// Critical: This function MUST be in RAM because it runs while XIP is disabled.
// We use __no_inline_not_in_flash_func to ensure it's not inlined into a flash function
// and is placed in RAM.
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

  const auto* flashPtr = reinterpret_cast<const uint8_t*>(getFlashAddress(offset));
  std::memcpy(buffer.data(), flashPtr, buffer.size());
  return true;
}

auto FlashManager::writeData(const uint32_t offset, const std::span<const uint8_t> data) -> bool
{
  if (offset + data.size() > PICO_FLASH_SIZE_BYTES)
  {
    return false;
  }

  // Align to page size if needed, but flash_range_program handles pages.
  // However, offset must be aligned to 256 bytes (FLASH_PAGE_SIZE) usually for programming?
  // Actually flash_range_program requires:
  // - addr aligned to 256 bytes (FLASH_PAGE_SIZE)
  // - count multiple of 256 bytes
  // Let's assume the caller handles alignment or we are writing full sectors/pages.
  // For WifiProvisioner, we write a full sector (4096).

  FlashOpContext ctx{offset, data.data(), data.size()};

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

  // Erase must be aligned to 4096 bytes (FLASH_SECTOR_SIZE)
  if ((offset % FLASH_SECTOR_SIZE != 0) or (size % FLASH_SECTOR_SIZE != 0))
  {
    printf("[FlashManager] Erase alignment error\n");
    return false;
  }

  FlashOpContext ctx{offset, nullptr, size};

  printf("[FlashManager] Erase: calling flash_safe_execute\n");
  fflush(stdout);
  vTaskSuspendAll();
  const auto result = flash_safe_execute(flashEraseTrampoline, &ctx, 2000);
  xTaskResumeAll();
  printf("[FlashManager] Erase: returned %d\n", result);
  fflush(stdout);

  if (result != PICO_OK)
  {
    printf("[FlashManager] Erase failed: %d\n", result);
    return false;
  }

  return true;
}
