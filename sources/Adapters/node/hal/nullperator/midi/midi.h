#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "driver/uart.h"

namespace NullperatorHAL::MIDI {
    esp_err_t Init();
    uart_port_t GetPort();
    esp_err_t Send(const uint8_t* data, size_t length);
    int Receive(uint8_t* data, size_t maxLength, uint32_t timeoutMs);
}
