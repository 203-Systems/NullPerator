#include "Adapters/node/hal/nullperator/imu/imu.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char* TAG = "NP_IMU";

namespace NullperatorHAL::IMU {
    esp_err_t Init() {
        constexpr uint8_t addresses[] = {0x6A, 0x6B};
        constexpr uint8_t whoAmIRegister = 0x0F;
        constexpr uint8_t expectedWhoAmI = 0x6A;

        i2c_master_bus_handle_t bus = System::GetI2CBus();
        if (!bus) {
            ESP_LOGE(TAG, "I2C bus is not initialized");
            return ESP_ERR_INVALID_STATE;
        }

        for (uint8_t address : addresses) {
            if (i2c_master_probe(bus, address, 50) != ESP_OK) {
                continue;
            }

            const i2c_device_config_t config = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = address,
                .scl_speed_hz = 400000,
                .scl_wait_us = 0,
                .flags = {},
            };
            i2c_master_dev_handle_t device = nullptr;
            esp_err_t ret = i2c_master_bus_add_device(bus, &config, &device);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add IMU at 0x%02X: %s", address,
                         esp_err_to_name(ret));
                return ESP_OK;
            }

            uint8_t whoAmI = 0;
            ret = i2c_master_transmit_receive(device, &whoAmIRegister, 1,
                                              &whoAmI, 1, 100);
            i2c_master_bus_rm_device(device);
            if (ret == ESP_OK && whoAmI == expectedWhoAmI) {
                ESP_LOGI(TAG, "LSM6DS3TR-C detected at 0x%02X", address);
                return ESP_OK;
            }
            ESP_LOGE(TAG, "Unexpected IMU at 0x%02X (WHO_AM_I=0x%02X)",
                     address, whoAmI);
            return ESP_OK;
        }

        ESP_LOGE(TAG, "LSM6DS3TR-C not found at 0x6A or 0x6B");
        return ESP_OK;
    }

    ImuData_t GetData() {
        return {};
    }
}
