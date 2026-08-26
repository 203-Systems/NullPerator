/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

namespace ui2 {

// The legacy Sample instrument exposes at most twenty focusable parameter
// rows. Keeping that exact upper bound gives UI2 full data parity without a
// heap-backed list; Name + Type + fields + OPAL operator rows still fit the
// fixed 32-bit enabled-row mask used by the embedded controller.
inline constexpr std::uint8_t kUiInstrumentMaximumFields = 20U;
inline constexpr std::uint8_t kUiInstrumentMaximumOperatorRows = 6U;
inline constexpr std::uint8_t kUiInstrumentMaximumRows =
    2U + kUiInstrumentMaximumFields + kUiInstrumentMaximumOperatorRows;

static_assert(kUiInstrumentMaximumRows <= 32U);

} // namespace ui2
