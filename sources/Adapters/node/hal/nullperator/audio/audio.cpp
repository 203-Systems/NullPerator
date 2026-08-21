#include "Adapters/node/hal/nullperator/audio/audio.h"
#include "board/pins.h"
#include "Adapters/node/hal/nullperator/audio/codec/es8389.h"
#include "Adapters/node/hal/nullperator/system/system.h"
#include "esp_log.h"

static const char* TAG = "NP_AUDIO";

namespace NullperatorHAL::Audio {
    static i2s_chan_handle_t txChan = nullptr;
    static i2s_chan_handle_t rxChan = nullptr;
    static OutputMode_t currentOutputMode = OUTPUT_OFF;
    static InputMode_t currentInputMode = INPUT_OFF;
    static uint8_t currentVolume = 50;

    namespace {
        constexpr i2s_data_bit_width_t I2S_AUDIO_BIT_WIDTH = I2S_DATA_BIT_WIDTH_16BIT;
        constexpr uint32_t I2S_DMA_FRAME_NUM = 256;
        constexpr uint32_t I2S_SAMPLE_RATE = 44100;
        esp_err_t sync_rx_slot_mode() {
            if (!rxChan) {
                return ESP_ERR_INVALID_STATE;
            }

            bool monoInput = (currentInputMode != INPUT_OFF) && (currentInputMode != INPUT_LINE_IN);
            i2s_std_slot_config_t slot_cfg =
                I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_AUDIO_BIT_WIDTH,
                                                    monoInput ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO);
            slot_cfg.slot_mask = monoInput ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH;

            esp_err_t ret = i2s_channel_reconfig_std_slot(rxChan, &slot_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reconfig RX slot mode: %s", esp_err_to_name(ret));
            }
            return ret;
        }
    }

    static esp_err_t sync_codec_state() {
        if (!NullperatorHAL::System::WriteIOExpanderPin(
                PCA_AUDIO_MUX_SEL, currentInputMode == INPUT_LINE_IN) ||
            !NullperatorHAL::System::WriteIOExpanderPin(
                PCA_PA_CTRL, currentOutputMode == OUTPUT_SPEAKER)) {
            return ESP_ERR_INVALID_STATE;
        }
        return Codec::ES8389::SyncState(currentOutputMode, currentInputMode);
    }

    esp_err_t Init() {
        if (!NullperatorHAL::System::GetI2CBus()) {
            ESP_LOGE(TAG, "System not initialized. Call System::Init() first.");
            return ESP_ERR_INVALID_STATE;
        }

        if (!txChan && !rxChan) {
            i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
            chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;

            esp_err_t ret = i2s_new_channel(&chan_cfg, &txChan, &rxChan);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
                return ret;
            }

            i2s_std_config_t std_cfg = {
                .clk_cfg = {
                    .sample_rate_hz = I2S_SAMPLE_RATE,
                    .clk_src = I2S_CLK_SRC_DEFAULT,
                    .ext_clk_freq_hz = 0,
                    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                },
                .slot_cfg =
                    I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_AUDIO_BIT_WIDTH, I2S_SLOT_MODE_STEREO),
                .gpio_cfg = {
                    .mclk = static_cast<gpio_num_t>(I2S_MCLK_PIN),
                    .bclk = static_cast<gpio_num_t>(I2S_BCLK_PIN),
                    .ws = static_cast<gpio_num_t>(I2S_LRCK_PIN),
                    .dout = static_cast<gpio_num_t>(I2S_DOUT_PIN),
                    .din = static_cast<gpio_num_t>(I2S_DIN_PIN),
                    .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
                },
            };

            ret = i2s_channel_init_std_mode(txChan, &std_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init I2S TX: %s", esp_err_to_name(ret));
                return ret;
            }

            ret = i2s_channel_init_std_mode(rxChan, &std_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init I2S RX: %s", esp_err_to_name(ret));
                return ret;
            }

            ret = i2s_channel_enable(txChan);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to enable I2S TX: %s", esp_err_to_name(ret));
                return ret;
            }
        }

        esp_err_t ret = Codec::ES8389::Init(txChan, rxChan);
        if (ret != ESP_OK) {
            return ret;
        }

        if (!NullperatorHAL::System::SetIOExpanderDirectionPin(PCA_PA_CTRL, false) ||
            !NullperatorHAL::System::SetIOExpanderDirectionPin(PCA_AUDIO_MUX_SEL, false) ||
            !NullperatorHAL::System::SetIOExpanderDirectionPin(PCA_PHONE_DET, true) ||
            !NullperatorHAL::System::WriteIOExpanderPin(PCA_PA_CTRL, false) ||
            !NullperatorHAL::System::WriteIOExpanderPin(PCA_AUDIO_MUX_SEL, false)) {
            ESP_LOGE(TAG, "Failed to configure audio IO expander pins");
            return ESP_FAIL;
        }

        currentOutputMode = OUTPUT_OFF;
        currentInputMode = INPUT_OFF;
        ret = sync_rx_slot_mode();
        if (ret != ESP_OK) {
            return ret;
        }
        ret = sync_codec_state();
        if (ret != ESP_OK) {
            return ret;
        }

        ret = SetVolume(currentVolume);
        if (ret != ESP_OK) {
            return ret;
        }

        ESP_LOGI(TAG, "Audio initialized");
        return ESP_OK;
    }

    i2s_chan_handle_t GetTxChannel() {
        return txChan;
    }

    i2s_chan_handle_t GetRxChannel() {
        return rxChan;
    }

    esp_err_t SetVolume(uint8_t volume) {
        if (volume > 100) {
            volume = 100;
        }
        currentVolume = volume;
        esp_err_t ret = Codec::ES8389::SetVolume(volume);
        if (ret != ESP_OK) {
            return ret;
        }

        ESP_LOGI(TAG, "Volume set to %d", volume);
        return ESP_OK;
    }

    uint8_t GetVolume() {
        return currentVolume;
    }

    esp_err_t SetOutputMode(OutputMode_t mode) {
        switch (mode) {
            case OUTPUT_OFF:
                ESP_LOGI(TAG, "Output mode: OFF");
                break;
            case OUTPUT_HEADPHONE:
                if (currentInputMode == INPUT_LINE_IN) {
                    currentInputMode = INPUT_OFF;
                    ESP_LOGI(TAG, "Input mode: OFF (HEADPHONE output selected)");
                }
                ESP_LOGI(TAG, "Output mode: HEADPHONE");
                break;
            case OUTPUT_SPEAKER:
                ESP_LOGI(TAG, "Output mode: SPEAKER");
                break;
            default:
                return ESP_ERR_INVALID_ARG;
        }

        currentOutputMode = mode;
        return sync_codec_state();
    }

    OutputMode_t GetOutputMode() {
        return currentOutputMode;
    }

    esp_err_t SetInputMode(InputMode_t mode) {
        switch (mode) {
            case INPUT_OFF:
                ESP_LOGI(TAG, "Input mode: OFF");
                break;
            case INPUT_ONBOARD_MIC:
                ESP_LOGI(TAG, "Input mode: ONBOARD_MIC");
                break;
            case INPUT_EARPHONE_MIC:
                ESP_LOGI(TAG, "Input mode: EARPHONE_MIC");
                break;
            case INPUT_LINE_IN:
                if (currentOutputMode == OUTPUT_HEADPHONE) {
                    currentOutputMode = OUTPUT_OFF;
                    ESP_LOGI(TAG, "Output mode: OFF (LINE_IN input selected)");
                }
                ESP_LOGI(TAG, "Input mode: LINE_IN");
                break;
            default:
                return ESP_ERR_INVALID_ARG;
        }

        currentInputMode = mode;
        esp_err_t ret = sync_rx_slot_mode();
        if (ret != ESP_OK) {
            return ret;
        }
        return sync_codec_state();
    }

    InputMode_t GetInputMode() {
        return currentInputMode;
    }

}
