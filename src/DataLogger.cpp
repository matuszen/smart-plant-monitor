#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

#include "Config.h"
#include "DataLogger.h"
#include "Types.h"

auto DataLogger::init() -> bool
{
  if (initialized_)
  {
    return true;
  }

  printf("[DataLogger] Initializing...\n");

  logs_.reserve(Config::MAX_LOG_ENTRIES);

  initialized_ = true;
  printf("[DataLogger] Initialization complete\n");
  return true;
}

void DataLogger::logData(const SensorData& data, const bool wasWatering)
{
  if (not initialized_)
  {
    return;
  }

  auto entry        = DataLogEntry{};
  entry.data        = data;
  entry.wasWatering = wasWatering;
  entry.uploaded    = false;
  entry.id          = nextLogId_++;

  logs_.push_back(entry);

  if (logs_.size() > Config::MAX_LOG_ENTRIES)
  {
    logs_.erase(logs_.begin());
  }

  const bool  waterAvailable = data.waterLevelAvailable;
  const bool  hasWaterData   = waterAvailable and data.water.isValid();
  const char* waterStatus    = waterAvailable ? "ERR" : "N/A";
  if (hasWaterData)
  {
    waterStatus = data.water.isLow() ? "LOW" : "OK";
  }
  const uint16_t waterRaw = hasWaterData ? data.water.rawValue : 0U;
  const float    waterPct = hasWaterData ? data.water.percentage : 0.0F;

  printf("[DataLogger] Logged entry #%u (Soil: %.1f%%, Temp: %.1f°C, Water: %s", entry.id, data.soil.percentage,
         data.environment.temperature, waterStatus);
  if (hasWaterData)
  {
    printf(" %.0f%% raw=%u", waterPct, waterRaw);
  }
  printf(")\n");
}

auto DataLogger::getUnuploadedLogs() const -> std::vector<DataLogEntry>
{
  auto result = std::vector<DataLogEntry>{};
  std::ranges::copy_if(logs_, std::back_inserter(result), [](const DataLogEntry& e) -> bool { return not e.uploaded; });
  return result;
}

void DataLogger::markAllAsUploaded()
{
  for (auto& log : logs_)
  {
    log.uploaded = true;
  }
  printf("[DataLogger] Marked %zu logs as uploaded\n", logs_.size());
}

void DataLogger::clearOldLogs(const size_t keepCount)
{
  if (logs_.size() <= keepCount)
  {
    return;
  }

  const auto toRemove = logs_.size() - keepCount;
  logs_.erase(logs_.begin(), logs_.begin() + int(toRemove));
  printf("[DataLogger] Cleared %d old logs\n", toRemove);
}
