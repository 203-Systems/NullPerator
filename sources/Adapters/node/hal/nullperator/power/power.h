#pragma once

#include "esp_err.h"

namespace NullperatorHAL::Power {
    esp_err_t Init();
    float GetBatteryVoltage();
    uint8_t GetBatteryPercentage();
    bool IsCharging();
}
