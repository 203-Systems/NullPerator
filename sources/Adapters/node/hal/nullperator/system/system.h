#pragma once

#include <cstdint>
#include "esp_err.h"
#include "driver/i2c_master.h"

namespace NullperatorHAL::System {
    esp_err_t Init();
    void EnterDeepSleep();

    i2c_master_bus_handle_t GetI2CBus();
    uint16_t ReadIOExpander();
    bool WriteIOExpander(uint16_t value);
    bool WriteIOExpanderPin(uint8_t pin, bool high);
    bool SetIOExpanderPolarity(uint16_t value);
    bool SetIOExpanderDirection(uint16_t value);
    bool SetIOExpanderDirectionPin(uint8_t pin, bool input);
}
