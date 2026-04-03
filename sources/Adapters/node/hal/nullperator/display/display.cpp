#include "Adapters/node/hal/nullperator/display/display.h"
#include "board/pins.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <cstdlib>
#include <cstring>

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY (5000)

static const char* TAG = "NP_DISPLAY";

namespace NullperatorHAL::Display {
    static esp_lcd_panel_handle_t panel = nullptr;
    static esp_lcd_panel_io_handle_t panelIO = nullptr;
    static uint8_t currentBrightness = 100;
    static bool spiInitialized = false;

    esp_err_t Init() {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_MODE,
            .duty_resolution = LEDC_DUTY_RES,
            .timer_num = LEDC_TIMER,
            .freq_hz = LEDC_FREQUENCY,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        esp_err_t ret = ledc_timer_config(&ledc_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
            return ret;
        }

        ledc_channel_config_t ledc_channel = {
            .gpio_num = DISPLAY_BL_PIN,
            .speed_mode = LEDC_MODE,
            .channel = LEDC_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        ret = ledc_channel_config(&ledc_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
            return ret;
        }

        if (!spiInitialized) {
            spi_bus_config_t buscfg = {
                .mosi_io_num = DISPLAY_MOSI_PIN,
                .miso_io_num = -1,
                .sclk_io_num = DISPLAY_SCK_PIN,
                .quadwp_io_num = -1,
                .quadhd_io_num = -1,
                .max_transfer_sz = WIDTH * static_cast<int>(HEIGHT * sizeof(uint16_t)),
            };

            ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
                return ret;
            }
            spiInitialized = true;
        }

        if (!panel) {
            esp_lcd_panel_io_spi_config_t io_config = {
                .cs_gpio_num = -1,
                .dc_gpio_num = DISPLAY_DC_PIN,
                .spi_mode = 3,
                .pclk_hz = 40 * 1000 * 1000,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            };

            ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panelIO);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
                return ret;
            }

            esp_lcd_panel_dev_config_t panel_config = {
                .reset_gpio_num = DISPLAY_RESET_PIN,
                .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                .bits_per_pixel = 16,
            };

            ret = esp_lcd_new_panel_st7789(panelIO, &panel_config, &panel);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
                return ret;
            }

            esp_lcd_panel_reset(panel);
            esp_lcd_panel_init(panel);
            esp_lcd_panel_invert_color(panel, true);
            esp_lcd_panel_mirror(panel, false, false);
            esp_lcd_panel_set_gap(panel, 0, 0);
            esp_lcd_panel_disp_on_off(panel, true);

        }

        SetBrightness(0); // Display turn on by app when it is ready

        ESP_LOGI(TAG, "Display initialized (%dx%d)", WIDTH, HEIGHT);
        return ESP_OK;
    }

    esp_lcd_panel_handle_t GetPanel() {
        return panel;
    }

    esp_lcd_panel_io_handle_t GetPanelIO() {
        return panelIO;
    }

    esp_err_t SetBrightness(uint8_t brightness) {
        currentBrightness = brightness;

        esp_err_t ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, brightness);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update LEDC duty: %s", esp_err_to_name(ret));
            return ret;
        }

        return ESP_OK;
    }

    uint8_t GetBrightness() {
        return currentBrightness;
    }
}
