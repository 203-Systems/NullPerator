/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include <cstddef>
namespace PlatformMemory {
// Temporary, non-realtime bulk storage. ESP32 uses PSRAM and fails if absent.
void *AllocateBulk(std::size_t bytes);
void FreeBulk(void *memory);
} // namespace PlatformMemory
