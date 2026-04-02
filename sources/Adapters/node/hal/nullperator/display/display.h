#pragma once

#include <cstdint>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

namespace NullperatorHAL::Display {
    constexpr int WIDTH = 240;
    constexpr int HEIGHT = 240;

    esp_err_t Init();
    esp_lcd_panel_handle_t GetPanel();

    esp_err_t SetBrightness(uint8_t brightness);
    uint8_t GetBrightness();
}
