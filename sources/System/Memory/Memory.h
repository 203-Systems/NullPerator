/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include <cstddef>
namespace PlatformMemory {
// Non-realtime bulk/model storage. ESP32 uses PSRAM and fails if absent.
void *AllocateBulk(std::size_t bytes);
void FreeBulk(void *memory);
} // namespace PlatformMemory
