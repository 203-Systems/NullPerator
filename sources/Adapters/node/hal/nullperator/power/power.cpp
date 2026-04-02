#include "Adapters/node/hal/nullperator/power/power.h"
#include "board/pins.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "NP_POWER";

namespace NullperatorHAL::Power {
    static adc_oneshot_unit_handle_t adcHandle = nullptr;
    static adc_cali_handle_t adcCaliHandle = nullptr;

    esp_err_t Init() {
        if (adcHandle) {
            return ESP_OK;
        }

        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };

        esp_err_t ret = adc_oneshot_new_unit(&init_config, &adcHandle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
            return ret;
        }

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };

        ret = adc_oneshot_config_channel(adcHandle, BATT_VOLTAGE_ADC_CHANNEL, &chan_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
            return ret;
        }

        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };

        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adcCaliHandle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC calibration not available, using raw values");
            adcCaliHandle = nullptr;
        }

        ESP_LOGI(TAG, "Power management initialized");
        return ESP_OK;
    }

    float GetBatteryVoltage() {
        if (!adcHandle) {
            return 0.0f;
        }

        int raw = 0;
        esp_err_t ret = adc_oneshot_read(adcHandle, BATT_VOLTAGE_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
            return 0.0f;
        }

        int voltage_mv = 0;
        if (adcCaliHandle) {
            adc_cali_raw_to_voltage(adcCaliHandle, raw, &voltage_mv);
        } else {
            voltage_mv = (raw * 3300) / 4095;
        }

        return (voltage_mv / 1000.0f) / BATT_VOLTAGE_DIV;
    }

    uint8_t GetBatteryPercentage() {
        constexpr float percentage_min = 0.0f;
        constexpr float percentage_max = 100.0f;

        if (BATT_VOLTAGE_FULL <= BATT_VOLTAGE_CUTOFF) {
            ESP_LOGW(TAG, "Invalid battery percentage range: cutoff=%.2f full=%.2f",
                     BATT_VOLTAGE_CUTOFF, BATT_VOLTAGE_FULL);
            return 0;
        }

        float voltage = GetBatteryVoltage();
        if (voltage <= BATT_VOLTAGE_CUTOFF) {
            return 0;
        }
        if (voltage >= BATT_VOLTAGE_FULL) {
            return 100;
        }

        float normalized = (voltage - BATT_VOLTAGE_CUTOFF) / (BATT_VOLTAGE_FULL - BATT_VOLTAGE_CUTOFF);
        float percentage = normalized * percentage_max;
        percentage = std::fmax(percentage_min, std::fmin(percentage_max, percentage));
        return static_cast<uint8_t>(std::lround(percentage));
    }

    bool IsCharging() {
        const uint16_t level = NullperatorHAL::System::ReadIOExpander();
        return (level & (1U << PCA_BTN_CHRG)) != 0;
    }
}
