#include "Adapters/node/hal/nullperator/system/system.h"
#include "Adapters/node/hal/nullperator/display/display.h"
#include "Adapters/node/hal/nullperator/system/io_expander.h"
#include "board/pins.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "NP_SYSTEM";

namespace {
    i2c_master_bus_handle_t g_busHandle = nullptr;
    i2c_master_dev_handle_t g_ioExpanderDev = nullptr;
    NullperatorHAL::System::IOExpander g_ioExpander = {};
    bool g_ioExpanderReady = false;

    esp_err_t init_i2c() {
        if (g_busHandle) {
            return ESP_OK;
        }

        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = static_cast<gpio_num_t>(I2C_SDA_PIN),
            .scl_io_num = static_cast<gpio_num_t>(I2C_SCL_PIN),
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {
                .enable_internal_pullup = true,
            },
        };

        esp_err_t ret = i2c_new_master_bus(&bus_config, &g_busHandle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGI(TAG, "I2C initialized on SDA=%d, SCL=%d", I2C_SDA_PIN, I2C_SCL_PIN);
        return ESP_OK;
    }

    esp_err_t init_io_expander() {
        if (g_ioExpanderReady) {
            return ESP_OK;
        }

        uint8_t detectedAddr = NullperatorHAL::System::IOExpander::kBaseAddress;
        bool found = false;
        for (uint8_t addr = NullperatorHAL::System::IOExpander::kBaseAddress;
             addr < (NullperatorHAL::System::IOExpander::kBaseAddress + 8); ++addr) {
            if (i2c_master_probe(g_busHandle, addr, 50) == ESP_OK) {
                detectedAddr = addr;
                found = true;
                break;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "No IO expander device found at 0x20-0x27");
            return ESP_OK;
        }

        const i2c_device_config_t ioExpanderCfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = detectedAddr,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
            .flags = {},
        };

        esp_err_t ret = i2c_master_bus_add_device(g_busHandle, &ioExpanderCfg, &g_ioExpanderDev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add IO expander device: %s", esp_err_to_name(ret));
            return ret;
        }

        g_ioExpander.Attach(g_ioExpanderDev);
        g_ioExpander.Write(NullperatorHAL::System::IOExpander::kAllHigh);
        g_ioExpanderReady = true;
        ESP_LOGI(TAG, "IO expander initialized at 0x%02X", detectedAddr);
        return ESP_OK;
    }
}

namespace NullperatorHAL::System {
    esp_err_t Init() {
        ESP_LOGI(TAG, "System initialization started");

        esp_err_t ret = init_i2c();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = init_io_expander();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "IO expander init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGI(TAG, "System initialized");
        return ESP_OK;
    }

    void EnterDeepSleep() {
        ESP_ERROR_CHECK(Display::Init());
        ESP_ERROR_CHECK(Display::SetBrightness(0));
        ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(
            static_cast<gpio_num_t>(FUNC_BTN_PIN), 0));
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(
            static_cast<gpio_num_t>(FUNC_BTN_PIN)));
        ESP_ERROR_CHECK(rtc_gpio_pullup_en(
            static_cast<gpio_num_t>(FUNC_BTN_PIN)));
        esp_deep_sleep_start();
    }

    i2c_master_bus_handle_t GetI2CBus() {
        return g_busHandle;
    }

    uint16_t ReadIOExpander() {
        return g_ioExpanderReady ? g_ioExpander.Read() : 0;
    }

    bool WriteIOExpander(uint16_t value) {
        return !g_ioExpanderReady || g_ioExpander.Write(value);
    }

    bool WriteIOExpanderPin(uint8_t pin, bool high) {
        if (pin > 15) {
            return false;
        }
        if (!g_ioExpanderReady) {
            return true;
        }
        return g_ioExpander.Write(static_cast<IOExpander::Port>(pin),
                                  high ? IOExpander::Level::High
                                       : IOExpander::Level::Low);
    }

    bool SetIOExpanderPolarity(uint16_t value) {
        return !g_ioExpanderReady || g_ioExpander.SetPolarity(value);
    }

    bool SetIOExpanderDirection(uint16_t value) {
        return !g_ioExpanderReady || g_ioExpander.SetDirection(value);
    }

    bool SetIOExpanderDirectionPin(uint8_t pin, bool input) {
        if (pin > 15) {
            return false;
        }
        if (!g_ioExpanderReady) {
            return true;
        }
        return g_ioExpander.SetDirection(static_cast<IOExpander::Port>(pin),
                                         input ? IOExpander::Direction::In
                                               : IOExpander::Direction::Out);
    }
}
