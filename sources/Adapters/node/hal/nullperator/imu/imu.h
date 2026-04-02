#pragma once

#include "esp_err.h"

namespace NullperatorHAL::IMU {
    struct ImuData_t {
        float accelX;
        float accelY;
        float accelZ;
        float gyroX;
        float gyroY;
        float gyroZ;
    };

    esp_err_t Init();
    ImuData_t GetData();
}
