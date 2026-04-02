#pragma once

#include "esp_err.h"

namespace NullperatorHAL::Input {
    struct ButtonState_t {
        bool select;
        bool start;
        bool rb;
        bool lb;
        bool a;
        bool b;
        bool up;
        bool down;
        bool left;
        bool right;
        bool func;
    };

    esp_err_t Init();
    ButtonState_t GetButtonState();
}
