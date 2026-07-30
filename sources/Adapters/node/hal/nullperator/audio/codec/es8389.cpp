#include "Adapters/node/hal/nullperator/audio/codec/es8389.h"
#include "board/pins.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "es8389_codec.h"

static const char* TAG = "NP_ES8389";

namespace NullperatorHAL::Codec::ES8389 {
    namespace {
        constexpr uint8_t REG_ADC_MODE_CONTROL = 0x23;
        constexpr uint8_t REG_ADC_ANALOG_ENABLE = 0x64;
        constexpr uint8_t REG_PGA1_GAIN = 0x72;
        constexpr uint8_t REG_PGA2_GAIN = 0x73;
        constexpr uint32_t CODEC_SAMPLE_RATE = 44100;
        constexpr uint8_t CODEC_CHANNELS = 2;
        constexpr uint8_t CODEC_BITS_PER_SAMPLE = 16;

        constexpr uint8_t ADC_ONBOARD_MIC = 0x8A;
        constexpr uint8_t ADC_EARPHONE_MIC = 0x85;
        constexpr uint8_t ADC_LINE_IN = 0x8F;
        constexpr uint8_t ADC_MODE_NORMAL = 0x00;
        constexpr uint8_t ADC_MODE_ADC1_TO_BOTH = 0x10;
        constexpr uint8_t ADC_MODE_ADC2_TO_BOTH = 0x20;
        constexpr uint8_t PGA_NO_INPUT = 0x00;
        constexpr uint8_t PGA_INPUT1_SINGLE_ENDED = 0x50;
        constexpr uint8_t PGA_INPUT2_SINGLE_ENDED = 0x60;

        const audio_codec_ctrl_if_t* s_codecCtrlIf = nullptr;
        const audio_codec_if_t* s_codecIf = nullptr;
        const audio_codec_data_if_t* s_codecDataIf = nullptr;
        esp_codec_dev_handle_t s_codecOutDev = nullptr;
        bool s_codecEnabled = false;

        esp_err_t write_reg(uint8_t reg, uint8_t value) {
            if (!s_codecOutDev) {
                return ESP_ERR_INVALID_STATE;
            }
            int ret = esp_codec_dev_write_reg(s_codecOutDev, reg, value);
            if (ret != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Failed to write reg 0x%02X: %d", reg, ret);
                return ESP_FAIL;
            }
            return ESP_CODEC_DEV_OK;
        }

        esp_err_t init_volume_dev(i2s_chan_handle_t txChan, i2s_chan_handle_t rxChan) {
            if (s_codecOutDev) {
                return ESP_OK;
            }
            if (!txChan || !rxChan) {
                return ESP_ERR_INVALID_STATE;
            }

            auto bus = NullperatorHAL::System::GetI2CBus();
            if (!bus) {
                return ESP_ERR_INVALID_STATE;
            }

            audio_codec_i2c_cfg_t i2c_cfg = {
                .port = 0,
                .addr = static_cast<uint8_t>(ES8389_ADDR << 1),
                .bus_handle = bus,
            };
            s_codecCtrlIf = audio_codec_new_i2c_ctrl(&i2c_cfg);
            if (!s_codecCtrlIf) {
                ESP_LOGE(TAG, "Failed to create codec I2C control interface");
                return ESP_FAIL;
            }

            es8389_codec_cfg_t codec_cfg = {
                .ctrl_if = s_codecCtrlIf,
                .gpio_if = nullptr,
                .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
                .pa_pin = -1,
                .pa_reverted = false,
                .master_mode = false,
                .use_mclk = true,
                .digital_mic = false,
                .invert_mclk = false,
                .invert_sclk = false,
                .hw_gain = {},
                .no_dac_ref = true,
                .mclk_div = 256,
            };
            s_codecIf = es8389_codec_new(&codec_cfg);
            if (!s_codecIf) {
                ESP_LOGE(TAG, "Failed to create ES8389 codec interface");
                return ESP_FAIL;
            }

            audio_codec_i2s_cfg_t i2s_cfg = {
                .rx_handle = rxChan,
                .tx_handle = txChan,
            };
            s_codecDataIf = audio_codec_new_i2s_data(&i2s_cfg);
            if (!s_codecDataIf) {
                ESP_LOGE(TAG, "Failed to create codec I2S data interface");
                return ESP_FAIL;
            }

            esp_codec_dev_cfg_t dev_cfg = {
                .dev_type = ESP_CODEC_DEV_TYPE_OUT,
                .codec_if = s_codecIf,
                .data_if = s_codecDataIf,
            };
            s_codecOutDev = esp_codec_dev_new(&dev_cfg);
            if (!s_codecOutDev) {
                ESP_LOGE(TAG, "Failed to create codec output device");
                return ESP_FAIL;
            }

            esp_codec_dev_sample_info_t fs = {
                .bits_per_sample = CODEC_BITS_PER_SAMPLE,
                .channel = CODEC_CHANNELS,
                .channel_mask = 0,
                .sample_rate = CODEC_SAMPLE_RATE,
                .mclk_multiple = 0,
            };
            int ret = esp_codec_dev_open(s_codecOutDev, &fs);
            if (ret != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Failed to open codec output device: %d", ret);
                return ESP_FAIL;
            }

            return ESP_OK;
        }

        esp_err_t configure_adc_input_path(Audio::InputMode_t inputMode) {
            uint8_t adcMode = ADC_MODE_NORMAL;
            uint8_t adcEnable = 0;
            uint8_t pga1 = PGA_NO_INPUT;
            uint8_t pga2 = PGA_NO_INPUT;
            bool inputActive = true;

            switch (inputMode) {
                case Audio::INPUT_OFF:
                    inputActive = false;
                    break;
                case Audio::INPUT_ONBOARD_MIC:
                    adcMode = ADC_MODE_ADC1_TO_BOTH;
                    adcEnable = ADC_ONBOARD_MIC;
                    pga1 = PGA_INPUT1_SINGLE_ENDED;
                    break;
                case Audio::INPUT_EARPHONE_MIC:
                    adcMode = ADC_MODE_ADC2_TO_BOTH;
                    adcEnable = ADC_EARPHONE_MIC;
                    pga2 = PGA_INPUT1_SINGLE_ENDED;
                    break;
                case Audio::INPUT_LINE_IN:
                    adcEnable = ADC_LINE_IN;
                    pga1 = PGA_INPUT2_SINGLE_ENDED;
                    pga2 = PGA_INPUT2_SINGLE_ENDED;
                    break;
                default:
                    return ESP_ERR_INVALID_ARG;
            }

            esp_err_t ret = write_reg(REG_ADC_MODE_CONTROL, adcMode);
            if (ret == ESP_OK) {
                ret = write_reg(REG_PGA1_GAIN, pga1);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_PGA2_GAIN, pga2);
            }
            if (ret == ESP_OK && inputActive) {
                ret = write_reg(REG_ADC_ANALOG_ENABLE, adcEnable);
            }
            return ret;
        }
    }

    esp_err_t Init(i2s_chan_handle_t txChan, i2s_chan_handle_t rxChan) {
        return init_volume_dev(txChan, rxChan);
    }

    esp_err_t SetVolume(uint8_t volume) {
        if (volume > 100) {
            volume = 100;
        }
        if (!s_codecOutDev) {
            ESP_LOGE(TAG, "Codec volume device not initialized");
            return ESP_ERR_INVALID_STATE;
        }

        int ret = esp_codec_dev_set_out_vol(s_codecOutDev, volume);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to set codec volume via esp_codec_dev: %d", ret);
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    esp_err_t SyncState(Audio::OutputMode_t outputMode, Audio::InputMode_t inputMode) {
        bool shouldEnable = (outputMode != Audio::OUTPUT_OFF) || (inputMode != Audio::INPUT_OFF);
        bool outputActive = outputMode != Audio::OUTPUT_OFF;

        int codecRet = esp_codec_dev_set_out_mute(s_codecOutDev, !outputActive);
        if (codecRet != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to set codec mute state: %d", codecRet);
            return ESP_FAIL;
        }

        esp_err_t ret = configure_adc_input_path(inputMode);
        if (ret != ESP_OK) {
            return ret;
        }

        bool wasEnabled = s_codecEnabled;
        s_codecEnabled = shouldEnable;
        if (wasEnabled != s_codecEnabled) {
            ESP_LOGI(TAG, "Codec %s", s_codecEnabled ? "enabled" : "disabled");
        }
        return ESP_OK;
    }

    bool IsEnabled() {
        return s_codecEnabled;
    }
}
