#include "Common.hpp"

#include <pico/time.h>

#include <cstdint>

auto Utils::getTimeSinceBoot() -> uint32_t
{
  return to_ms_since_boot(get_absolute_time());
}
