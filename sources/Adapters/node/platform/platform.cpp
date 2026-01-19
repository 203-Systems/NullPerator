#include "platform.h"
#include "Adapters/node/mutex/Mutex.h"
#include "PCA95x5.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/rtc_io.h"
#include "esp_private/usb_phy.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8389_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>

i2c_master_bus_handle_t i2c_handle = NULL;
usb_phy_handle_t usb_phy_hdl = NULL;

i2c_master_dev_handle_t io_expander_handle = NULL;
PCA95x5::PCA95x5 io_expander;

// Audio codec globals
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static esp_codec_dev_handle_t codec_handle = NULL;
static int32_t codec_volume = 50;
static bool codec_muted = false;
static bool audio_i2s_ready = false;

static constexpr uint8_t kCodecAddr7bit = 0x10; // Fixed strap

extern "C" {
void board_init() {
    // Enable system power domain
    // gpio_reset_pin((gpio_num_t)POWER_EN_PIN);
    // gpio_set_direction((gpio_num_t)POWER_EN_PIN, GPIO_MODE_OUTPUT);
    // gpio_set_level((gpio_num_t)POWER_EN_PIN, 1);

    gpio_reset_pin((gpio_num_t)INPUT_MENU_PIN);
    gpio_set_direction((gpio_num_t)INPUT_MENU_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)INPUT_MENU_PIN, GPIO_PULLUP_ONLY);

    // // Enable USB
    // usb_phy_config_t usb_phy_conf = {
    //     .controller = USB_PHY_CTRL_OTG,
    //     .target = USB_PHY_TARGET_INT,
    //     .otg_mode = USB_OTG_MODE_DEVICE,
    //     .otg_speed = USB_PHY_SPEED_UNDEFINED,
    // };

    // ESP_ERROR_CHECK(usb_new_phy(&usb_phy_conf, &usb_phy_hdl));

    // Enable I2C
    gpio_reset_pin((gpio_num_t)I2C_SDA_PIN);
    gpio_reset_pin((gpio_num_t)I2C_SCL_PIN);
    gpio_set_direction((gpio_num_t)I2C_SDA_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction((gpio_num_t)I2C_SCL_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode((gpio_num_t)I2C_SDA_PIN, GPIO_FLOATING);
    gpio_set_pull_mode((gpio_num_t)I2C_SCL_PIN, GPIO_FLOATING);
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)I2C_SDA_PIN,
        .scl_io_num = (gpio_num_t)I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        // Match ESP BSP style: enable internal pull-ups (often improves I2C reliability).
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_handle));

    // Set up the io expander i2c device
    bool io_expander_found = false;
    uint8_t io_expander_addr = PCA95x5::BASE_I2C_ADDR;
    for (uint8_t addr = PCA95x5::BASE_I2C_ADDR; addr < (PCA95x5::BASE_I2C_ADDR + 8); ++addr) {
        if (i2c_master_probe(i2c_handle, addr, 50) == ESP_OK) {
            io_expander_found = true;
            io_expander_addr = addr;
            break;
        }
    }

    if (!io_expander_found) {
        ESP_LOGE("KEYPAD", "No PCA95x5 device found at 0x20-0x27");
    } else {
        ESP_LOGI("KEYPAD", "Detected PCA95x5 at 0x%02X", io_expander_addr);
    }

    i2c_device_config_t io_expander_cfg = {
    .device_address = io_expander_addr,
    .scl_speed_hz = 400000,
    };

    if (io_expander_found) {
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_handle, &io_expander_cfg, &io_expander_handle));
    }

    // Set up the io expander
    if (io_expander_found) {
        io_expander.attach(io_expander_handle);

        // Drive all outputs high by default (safe idle state).
        io_expander.write(PCA95x5::Level::H_ALL);

        // Set specific pins as input (binary: 1111100111111111)
        if (!io_expander.direction(0b1111100111111111)) {
            ESP_LOGE("KEYPAD", "Set dir returned error");
        }

        // Set polarity inversion for active-low button wiring (buttons to GND).
        if (!io_expander.polarity(0b1111100111111111)) {
            ESP_LOGE("KEYPAD", "Set polarity returned error");
        }

        ESP_LOGI("KEYPAD", "IO expander input: 0x%04X", io_expander.read());

        switch_audio_mode(headphone_out);
        switch_speaker_mode(false);
    }
}

void platform_init() {

}

uint16_t get_io_expander_input() {
    if (!io_expander_handle) {
        return 0;
    }
    return io_expander.read();
}

void switch_audio_mode(audio_mode mode) {
    if (!io_expander_handle) {
        (void)mode;
        return;
    }
    if(mode == headphone_out) {
        io_expander.write((PCA95x5::Port::Port)OUTPUT_AUDIO_MUX_SEL, PCA95x5::Level::L);
    } else if(mode == line_in) {
        io_expander.write((PCA95x5::Port::Port)OUTPUT_AUDIO_MUX_SEL, PCA95x5::Level::H);
    }
}

void switch_speaker_mode(bool on) {
    if (!io_expander_handle) {
        (void)on;
        return;
    }
    ESP_LOGI("AUDIO_ROUTE", "Speaker %s", on ? "ON" : "OFF");
    io_expander.write((PCA95x5::Port::Port)OUTPUT_PA_CTRL, on ? PCA95x5::Level::H : PCA95x5::Level::L);
}

void enter_sleep() {
    gpio_set_level((gpio_num_t)DISPLAY_BL_PIN, 0);
    // gpio_set_level((gpio_num_t)POWER_EN_PIN, 0);

    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup((gpio_num_t)INPUT_MENU_PIN, 0));

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis((gpio_num_t)INPUT_MENU_PIN));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en((gpio_num_t)INPUT_MENU_PIN));

    esp_deep_sleep_start();
}

uint32_t millis(void) {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t micros(void) { return static_cast<uint32_t>(esp_timer_get_time()); }

// Audio codec API implementation
esp_err_t audio_codec_init(void) {
    if (audio_i2s_ready || codec_handle) {
        return ESP_OK; // Already initialized
    }

    // These pins overlap the ESP32-S3 JTAG pads on many modules (GPIO39-42). Make sure they're
    // in GPIO mode before routing them through the GPIO matrix for I2S.
    gpio_reset_pin((gpio_num_t)I2S_MCLK_PIN);
    gpio_reset_pin((gpio_num_t)I2S_BCLK_PIN);
    gpio_reset_pin((gpio_num_t)I2S_LRCK_PIN);
    gpio_reset_pin((gpio_num_t)I2S_DOUT_PIN);
    gpio_reset_pin((gpio_num_t)I2S_DIN_PIN);
    gpio_set_pull_mode((gpio_num_t)I2S_BCLK_PIN, GPIO_FLOATING);
    gpio_set_pull_mode((gpio_num_t)I2S_LRCK_PIN, GPIO_FLOATING);
    gpio_set_pull_mode((gpio_num_t)I2S_DOUT_PIN, GPIO_FLOATING);
    gpio_set_pull_mode((gpio_num_t)I2S_DIN_PIN, GPIO_FLOATING);
    gpio_set_pull_mode((gpio_num_t)I2S_MCLK_PIN, GPIO_FLOATING);

    // Initialize I2S channels
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Match ESP BSP pattern: clear legacy DMA samples automatically.
    chan_cfg.auto_clear = true;
    // Recording isn't implemented on node yet, so keep RX disabled to avoid reconfiguring the shared I2S peripheral.
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_chan, NULL));
    i2s_rx_chan = NULL;

    // Configure I2S in standard mode for TX (playback)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCLK_PIN,
            .bclk = (gpio_num_t)I2S_BCLK_PIN,
            .ws = (gpio_num_t)I2S_LRCK_PIN,
            .dout = (gpio_num_t)I2S_DOUT_PIN,
            .din = (gpio_num_t)I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (i2s_tx_chan) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_chan, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_chan));
    }

    audio_i2s_ready = true;

    // Create I2S data interface
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = i2s_rx_chan,
        .tx_handle = i2s_tx_chan,
        .clk_src = 0,
    };
    const audio_codec_data_if_t* i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);

    // Initialize ES8389-family codec
    const audio_codec_gpio_if_t* gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        // esp_codec_dev's I2C control expects an 8-bit I2C address byte (7-bit << 1).
        .addr = static_cast<uint8_t>(kCodecAddr7bit << 1),
        .bus_handle = i2c_handle,
    };
    const audio_codec_ctrl_if_t* i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es8389_codec_cfg_t es8389_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {},
        .no_dac_ref = false,
        .mclk_div = 256,
    };
    const audio_codec_if_t* es8389_dev = es8389_codec_new(&es8389_cfg);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8389_dev,
        .data_if = i2s_data_if,
    };
    codec_handle = esp_codec_dev_new(&codec_dev_cfg);
    if (!codec_handle) {
        ESP_LOGE("ES8389", "Failed to create codec handle");
        return ESP_OK;
    }

    // Open codec and set default volume
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = 44100,
        .mclk_multiple = 0,
    };
    const int open_ret = esp_codec_dev_open(codec_handle, &fs);
    if (open_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE("ES8389", "Failed to open codec: %d", open_ret);
        return ESP_OK;
    }
    (void)esp_codec_dev_set_out_mute(codec_handle, false);
    (void)esp_codec_dev_set_out_vol(codec_handle, static_cast<int>(codec_volume));
    int id1 = 0;
    int id2 = 0;
    if (esp_codec_dev_read_reg(codec_handle, 0xFD, &id1) == ESP_CODEC_DEV_OK &&
        esp_codec_dev_read_reg(codec_handle, 0xFE, &id2) == ESP_CODEC_DEV_OK) {
        ESP_LOGI("ES8389", "CHIP_ID: %02X %02X", id1 & 0xFF, id2 & 0xFF);
    }
    int dac_l = 0;
    int dac_r = 0;
    if (esp_codec_dev_read_reg(codec_handle, 0x46, &dac_l) == ESP_CODEC_DEV_OK &&
        esp_codec_dev_read_reg(codec_handle, 0x47, &dac_r) == ESP_CODEC_DEV_OK) {
        ESP_LOGI("ES8389", "DAC_VOL_REG: L=%02X R=%02X", dac_l & 0xFF, dac_r & 0xFF);
    }

    return ESP_OK;
}

esp_err_t audio_codec_write(void* buffer, size_t len, size_t* bytes_written, uint32_t timeout_ms) {
    if (!audio_i2s_ready || !i2s_tx_chan) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bytes_written) {
        *bytes_written = len;
    }
    if (codec_muted || codec_volume <= 0) {
        return ESP_OK;
    }
    if (codec_handle) {
        (void)timeout_ms;
        const int ret = esp_codec_dev_write(codec_handle, buffer, static_cast<int>(len));
        if (ret == ESP_CODEC_DEV_OK) {
            return ESP_OK;
        }
        if (ret == ESP_CODEC_DEV_WRONG_STATE) {
            ESP_LOGW("ES8389", "esp_codec_dev_write wrong state");
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGW("ES8389", "esp_codec_dev_write failed (%d)", ret);
        return ESP_FAIL;
    }
    return i2s_channel_write(i2s_tx_chan, buffer, len, bytes_written, timeout_ms);
}

esp_err_t audio_codec_set_volume(int volume) {
    codec_volume = volume;
    codec_muted = (volume <= 0);
    if (codec_handle) {
        if (volume <= 0) {
            return esp_codec_dev_set_out_mute(codec_handle, true);
        } else {
            esp_codec_dev_set_out_mute(codec_handle, false);
            return esp_codec_dev_set_out_vol(codec_handle, volume);
        }
    }
    return ESP_OK;
}

int audio_codec_get_volume(void) {
    return static_cast<int>(codec_volume);
}

esp_err_t audio_codec_set_mute(bool enable) {
    codec_muted = enable;
    if (codec_handle) {
        return esp_codec_dev_set_out_mute(codec_handle, enable);
    }
    return ESP_OK;
}

} // extern "C"

SysMutex *platform_mutex() { return new NodeMutex(); }
