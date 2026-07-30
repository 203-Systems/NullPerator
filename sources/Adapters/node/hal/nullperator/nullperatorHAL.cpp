#include "nullperatorHAL.h"
#include "esp_log.h"

static const char* TAG = "NP_HAL";

namespace {
    esp_err_t keep_first_error(esp_err_t current, esp_err_t next) {
        return (current == ESP_OK) ? next : current;
    }
}

namespace NullperatorHAL {
    esp_err_t Init() {
        ESP_LOGI(TAG, "Full board init started");

        esp_err_t first_error = System::Init();
        if (first_error != ESP_OK) {
            ESP_LOGE(TAG, "System init failed: %s", esp_err_to_name(first_error));
            return first_error;
        }

        esp_err_t ret = Power::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Power init failed: %s", esp_err_to_name(ret));
            first_error = keep_first_error(first_error, ret);
        }

        ret = Input::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Input init failed: %s", esp_err_to_name(ret));
            first_error = keep_first_error(first_error, ret);
        }

        ret = Audio::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Audio init failed: %s", esp_err_to_name(ret));
            first_error = keep_first_error(first_error, ret);
        }

        ret = Display::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
            first_error = keep_first_error(first_error, ret);
        }

        ret = IMU::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "IMU init failed: %s", esp_err_to_name(ret));
            first_error = keep_first_error(first_error, ret);
        }

        ret = Storage::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Storage init failed: %s", esp_err_to_name(ret));
        }

        ret = MIDI::Init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "MIDI init failed: %s", esp_err_to_name(ret));
            first_error = keep_first_error(first_error, ret);
        }

        ESP_LOGI(TAG, "Full board init completed");
        return first_error;
    }
}
