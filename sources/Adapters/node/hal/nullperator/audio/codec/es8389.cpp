#include "Adapters/node/hal/nullperator/audio/codec/es8389.h"
#include "board/pins.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "audio_codec_if.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char* TAG = "NP_ES8389";

namespace NullperatorHAL::Codec::ES8389 {
    namespace {
        constexpr int I2C_TIMEOUT_MS = 100;
        constexpr uint8_t REG_ISOLATION = 0xF3;
        constexpr uint8_t REG_RESET = 0x00;
        constexpr uint8_t REG_ADC_MODE_CONTROL = 0x23;
        constexpr uint8_t REG_ANALOG_ENABLE1 = 0x61;
        constexpr uint8_t REG_ADC_ANALOG_ENABLE = 0x64;
        constexpr uint8_t REG_DAC_ANALOG_CONTROL = 0x69;
        constexpr uint8_t REG_DAC1_VOLUME = 0x46;
        constexpr uint8_t REG_DAC2_VOLUME = 0x47;
        constexpr uint8_t REG_PGA1_GAIN = 0x72;
        constexpr uint8_t REG_PGA2_GAIN = 0x73;

        constexpr uint8_t ISOLATION_NORMAL = 0x00;
        constexpr uint8_t RESET_RUN = 0x00;
        constexpr uint8_t RESET_POWERDOWN = 0x01;
        constexpr uint8_t ANALOG_DISABLE = 0x08;
        constexpr uint8_t ANALOG_INPUT_ACTIVE = 0xC8;
        constexpr uint8_t ANALOG_OUTPUT_ACTIVE = 0xE8;
        constexpr uint8_t ADC_DISABLE = 0x30;
        constexpr uint8_t ADC_ONBOARD_MIC = 0x8A;
        constexpr uint8_t ADC_EARPHONE_MIC = 0x85;
        constexpr uint8_t ADC_LINE_IN = 0x8F;
        constexpr uint8_t DAC_OFF = 0xA0;
        constexpr uint8_t DAC_LINEOUT = 0x00;
        constexpr uint8_t DAC_HEADPHONE = 0x03;
        constexpr uint8_t ADC_MODE_NORMAL = 0x00;
        constexpr uint8_t ADC_MODE_ADC1_TO_BOTH = 0x10;
        constexpr uint8_t ADC_MODE_ADC2_TO_BOTH = 0x20;
        constexpr uint8_t PGA_NO_INPUT = 0x00;
        constexpr uint8_t PGA_INPUT1_SINGLE_ENDED = 0x50;
        constexpr uint8_t PGA_INPUT2_SINGLE_ENDED = 0x60;

        struct LocalCodecIf {
            audio_codec_if_t base;
            bool isOpen;
        };

        i2c_master_dev_handle_t s_codecHandle = nullptr;
        const audio_codec_data_if_t* s_codecDataIf = nullptr;
        esp_codec_dev_handle_t s_codecOutDev = nullptr;
        LocalCodecIf s_codecIf = {};
        bool s_codecEnabled = false;

        static const esp_codec_dev_vol_range_t kCodecVolRange = {
            .min_vol = {
                .vol = 0x00,
                .db_value = -95.5f,
            },
            .max_vol = {
                .vol = 0xFF,
                .db_value = 32.0f,
            },
        };

        esp_err_t init_codec_handle() {
            if (s_codecHandle) {
                return ESP_OK;
            }

            auto bus = NullperatorHAL::System::GetI2CBus();
            if (!bus) {
                return ESP_ERR_INVALID_STATE;
            }

            i2c_device_config_t codec_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = ES8390_ADDR,
                .scl_speed_hz = 100000,
                .scl_wait_us = 0,
                .flags = {},
            };

            esp_err_t ret = i2c_master_bus_add_device(bus, &codec_cfg, &s_codecHandle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add codec device: %s", esp_err_to_name(ret));
            }
            return ret;
        }

        esp_err_t write_reg(uint8_t reg, uint8_t value) {
            esp_err_t ret = init_codec_handle();
            if (ret != ESP_OK) {
                return ret;
            }

            const uint8_t data[] = {reg, value};
            ret = i2c_master_transmit(s_codecHandle, data, sizeof(data), I2C_TIMEOUT_MS);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write reg 0x%02X: %s", reg, esp_err_to_name(ret));
            }
            return ret;
        }

        esp_err_t read_reg(uint8_t reg, uint8_t* value) {
            esp_err_t ret = init_codec_handle();
            if (ret != ESP_OK) {
                return ret;
            }

            ret = i2c_master_transmit_receive(s_codecHandle, &reg, sizeof(reg), value, 1, I2C_TIMEOUT_MS);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read reg 0x%02X: %s", reg, esp_err_to_name(ret));
            }
            return ret;
        }

        int local_codec_open(const audio_codec_if_t* h, void* cfg, int cfg_size) {
            (void)cfg;
            (void)cfg_size;
            auto* codec = (LocalCodecIf*)h;
            codec->isOpen = true;
            return ESP_CODEC_DEV_OK;
        }

        bool local_codec_is_open(const audio_codec_if_t* h) {
            return ((const LocalCodecIf*)h)->isOpen;
        }

        int local_codec_set_vol(const audio_codec_if_t* h, float db_value) {
            (void)h;
            int reg = esp_codec_dev_vol_calc_reg(&kCodecVolRange, db_value);
            esp_err_t ret = write_reg(REG_DAC1_VOLUME, static_cast<uint8_t>(reg));
            if (ret == ESP_OK) {
                ret = write_reg(REG_DAC2_VOLUME, static_cast<uint8_t>(reg));
            }
            return ret == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
        }

        int local_codec_set_reg(const audio_codec_if_t* h, int reg, int value) {
            (void)h;
            return write_reg(static_cast<uint8_t>(reg), static_cast<uint8_t>(value)) == ESP_OK ?
                ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
        }

        int local_codec_get_reg(const audio_codec_if_t* h, int reg, int* value) {
            (void)h;
            uint8_t regValue = 0;
            esp_err_t ret = read_reg(static_cast<uint8_t>(reg), &regValue);
            if (ret != ESP_OK) {
                return ESP_CODEC_DEV_READ_FAIL;
            }
            *value = regValue;
            return ESP_CODEC_DEV_OK;
        }

        esp_err_t init_volume_dev(i2s_chan_handle_t txChan, i2s_chan_handle_t rxChan) {
            if (s_codecOutDev) {
                return ESP_OK;
            }
            if (!txChan || !rxChan) {
                return ESP_ERR_INVALID_STATE;
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

            s_codecIf.base.open = local_codec_open;
            s_codecIf.base.is_open = local_codec_is_open;
            s_codecIf.base.set_vol = local_codec_set_vol;
            s_codecIf.base.set_reg = local_codec_set_reg;
            s_codecIf.base.get_reg = local_codec_get_reg;
            s_codecIf.base.close = nullptr;
            s_codecIf.isOpen = false;
            s_codecIf.base.open(&s_codecIf.base, nullptr, 0);

            esp_codec_dev_cfg_t dev_cfg = {
                .dev_type = ESP_CODEC_DEV_TYPE_OUT,
                .codec_if = &s_codecIf.base,
                .data_if = s_codecDataIf,
            };
            s_codecOutDev = esp_codec_dev_new(&dev_cfg);
            if (!s_codecOutDev) {
                ESP_LOGE(TAG, "Failed to create codec output device");
                return ESP_FAIL;
            }

            return ESP_OK;
        }

        esp_err_t configure_adc_input_path(Audio::InputMode_t inputMode) {
            uint8_t adcMode = ADC_MODE_NORMAL;
            uint8_t adcEnable = ADC_DISABLE;
            uint8_t pga1 = PGA_NO_INPUT;
            uint8_t pga2 = PGA_NO_INPUT;

            switch (inputMode) {
                case Audio::INPUT_OFF:
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
            if (ret == ESP_OK) {
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
        bool inputActive = inputMode != Audio::INPUT_OFF;
        bool outputActive = outputMode != Audio::OUTPUT_OFF;

        esp_err_t ret = write_reg(REG_ISOLATION, ISOLATION_NORMAL);
        if (ret != ESP_OK) {
            return ret;
        }

        if (!shouldEnable) {
            ret = write_reg(REG_DAC_ANALOG_CONTROL, DAC_OFF);
            if (ret == ESP_OK) {
                ret = write_reg(REG_ADC_MODE_CONTROL, ADC_MODE_NORMAL);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_PGA1_GAIN, PGA_NO_INPUT);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_PGA2_GAIN, PGA_NO_INPUT);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_ADC_ANALOG_ENABLE, ADC_DISABLE);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_ANALOG_ENABLE1, ANALOG_DISABLE);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_RESET, RESET_POWERDOWN);
            }
        } else {
            uint8_t analogEnable = outputActive ? ANALOG_OUTPUT_ACTIVE : ANALOG_INPUT_ACTIVE;
            uint8_t dacAnalog = DAC_OFF;
            if (outputMode == Audio::OUTPUT_HEADPHONE) {
                dacAnalog = DAC_HEADPHONE;
            } else if (outputMode == Audio::OUTPUT_SPEAKER) {
                dacAnalog = DAC_LINEOUT;
            }

            ret = write_reg(REG_RESET, RESET_RUN);
            if (ret == ESP_OK) {
                ret = write_reg(REG_ANALOG_ENABLE1, analogEnable);
            }
            if (ret == ESP_OK) {
                ret = inputActive ? configure_adc_input_path(inputMode)
                                  : write_reg(REG_ADC_ANALOG_ENABLE, ADC_DISABLE);
            }
            if (ret == ESP_OK) {
                ret = write_reg(REG_DAC_ANALOG_CONTROL, dacAnalog);
            }
        }

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
