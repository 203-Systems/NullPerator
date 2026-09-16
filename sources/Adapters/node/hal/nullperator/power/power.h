#pragma once

#include "esp_err.h"
#include <cstdint>

namespace NullperatorHAL::Power {
esp_err_t Init();
float GetBatteryVoltage();
uint8_t GetBatteryPercentage();
// Returns false for an unavailable sample; charging is unchanged on failure.
bool ReadChargingState(bool &charging);
// Compatibility helper: an unavailable sample never reports charging.
bool IsCharging();
} // namespace NullperatorHAL::Power
