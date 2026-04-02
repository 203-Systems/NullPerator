#include "Adapters/node/hal/nullperator/imu/imu.h"
#include "esp_log.h"

static const char* TAG = "NP_IMU";

namespace NullperatorHAL::IMU {
    esp_err_t Init() {
        ESP_LOGW(TAG, "IMU driver not implemented yet");
        return ESP_OK;
    }

    ImuData_t GetData() {
        return {};
    }
}
