/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <array>
#include <cstdint>

namespace ui2 {

struct UiCursorElement {
  // Coverage data belongs to the element. UiPalette only caches the generated
  // indexed colors after a user theme changes.
  static constexpr std::uint8_t kCornerAlpha = 0x6B;
  static constexpr std::array<std::uint8_t, 3> kWaveformCoverageQuarters{
      1, 2, 3};
};

} // namespace ui2
