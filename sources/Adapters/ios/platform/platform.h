#pragma once

#include "System/Process/SysMutex.h"

#include <cstdint>

void platform_init();
SysMutex *platform_mutex();
std::uint32_t millis();
std::uint32_t micros();
void platform_brightness(std::uint8_t value);
