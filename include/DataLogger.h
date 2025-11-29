#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Types.h"

class DataLogger final
{
public:
  DataLogger()  = default;
  ~DataLogger() = default;

  DataLogger(const DataLogger&)                        = delete;
  auto operator=(const DataLogger&) -> DataLogger&     = delete;
  DataLogger(DataLogger&&) noexcept                    = delete;
  auto operator=(DataLogger&&) noexcept -> DataLogger& = delete;

  auto init() -> bool;

  void logData(const SensorData& data, bool wasWatering);

  [[nodiscard]] auto getAllLogs() const -> const std::vector<DataLogEntry>&
  {
    return logs_;
  }
  [[nodiscard]] auto getUnuploadedLogs() const -> std::vector<DataLogEntry>;

  void markAllAsUploaded();
  void clearOldLogs(size_t keepCount = 100);

  [[nodiscard]] auto getLogCount() const -> size_t
  {
    return logs_.size();
  }
  [[nodiscard]] auto isInitialized() const -> bool
  {
    return initialized_;
  }

private:
  bool                      initialized_{false};
  std::vector<DataLogEntry> logs_;
  uint32_t                  nextLogId_{1};
};
