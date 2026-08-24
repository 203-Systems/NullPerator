/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Direct, synchronous RGB565 transport shared by the UI2 product path and the
// legacy reference renderer. Pixels must already use the byte order expected
// by the ESP LCD panel. The caller may reuse its buffer when the call returns.
void display_rgb565_transport_init(void);
bool display_draw_rgb565_region(uint16_t x, uint16_t y, uint16_t width,
                                uint16_t height,
                                const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
