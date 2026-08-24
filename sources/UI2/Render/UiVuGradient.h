/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Theme/UiPalette.h"

#include <cstdint>

namespace ui2 {

class UiVuGradient {
public:
  static constexpr PaletteIndex kFirstIndex = UiPalette::kFirstDynamicIndex;
  static constexpr std::uint16_t kMaximumHeight = 153;

  static bool Configure(UiPalette &palette, std::uint16_t height);
  [[nodiscard]] static constexpr PaletteIndex IndexAt(std::uint16_t row) {
    return static_cast<PaletteIndex>(kFirstIndex + row);
  }
};

static_assert(UiVuGradient::kFirstIndex + UiVuGradient::kMaximumHeight <= 256);

} // namespace ui2
