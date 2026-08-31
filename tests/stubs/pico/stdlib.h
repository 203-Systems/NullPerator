/* Minimal Pico SDK surface for host-side Braids lifecycle coverage. */
#pragma once

#include <cstdint>
#include <cstring>

#define SYS_MEMSET std::memset

inline std::uint32_t platform_get_rand() { return 0x12345678U; }
