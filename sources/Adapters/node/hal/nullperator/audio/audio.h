#pragma once

#include <cstdint>
#include "esp_err.h"
#include "driver/i2s_std.h"

namespace NullperatorHAL::Audio {
    enum OutputMode_t {
        OUTPUT_OFF,
        OUTPUT_HEADPHONE,
        OUTPUT_SPEAKER
    };

    enum InputMode_t {
        INPUT_OFF,
        INPUT_ONBOARD_MIC,
        INPUT_EARPHONE_MIC,
        INPUT_LINE_IN
    };

    esp_err_t Init();
    i2s_chan_handle_t GetTxChannel();
    i2s_chan_handle_t GetRxChannel();

    esp_err_t SetVolume(uint8_t volume);
    uint8_t GetVolume();

    esp_err_t SetOutputMode(OutputMode_t mode);
    OutputMode_t GetOutputMode();
    esp_err_t SetInputMode(InputMode_t mode);
    InputMode_t GetInputMode();

    bool IsHeadphoneConnected();
}
