/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_WASM_PROCESS_H
#define PICOTRACKER_WASM_PROCESS_H

#include "System/Process/SysMutex.h"

#include <cstdint>

namespace WasmProcess {

bool PowerDown();
bool EnterBootloader();
bool Reboot();

} // namespace WasmProcess

void platform_init();
SysMutex *platform_mutex();
std::uint32_t millis();
std::uint32_t micros();
void platform_brightness(std::uint8_t value);

#endif
