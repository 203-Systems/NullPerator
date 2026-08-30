/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>

// Periodically reports the smallest amount of unused stack observed by the
// current task. ESP-IDF expresses both task stack sizes and the high-water mark
// in bytes on this target; sizeof(StackType_t) keeps the conversion explicit.
class NodeTaskStackTelemetry {
public:
  explicit NodeTaskStackTelemetry(const char *taskName)
      : taskName_(taskName), nextReport_(xTaskGetTickCount() + kReportPeriod) {}

  ~NodeTaskStackTelemetry() { Report(); }

  void Poll() {
    const TickType_t now = xTaskGetTickCount();
    if (static_cast<std::int32_t>(now - nextReport_) < 0)
      return;
    Report();
    nextReport_ = now + kReportPeriod;
  }

private:
  void Report() const {
    const UBaseType_t freeStackUnits = uxTaskGetStackHighWaterMark(nullptr);
    const std::size_t freeStackBytes =
        static_cast<std::size_t>(freeStackUnits) * sizeof(StackType_t);
    ESP_LOGI("TASK_STACK", "%s minimum free stack: %u bytes", taskName_,
             static_cast<unsigned>(freeStackBytes));
  }

  static constexpr TickType_t kReportPeriod = pdMS_TO_TICKS(30000U);
  const char *taskName_;
  TickType_t nextReport_;
};
