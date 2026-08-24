/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <cstdint>

namespace ui2 {

// Song, Phrase and Table deliberately share one vertical rhythm. Keeping the
// metrics here prevents one tracker page from silently packing its 16 rows
// more tightly than the others.
struct UiTrackerGridMetrics {
  static constexpr std::int16_t kHeaderTextY = 38;
  // Two visible pixels separate the 5x7 header glyphs from row 00.
  static constexpr std::int16_t kFirstRowTextY = 48;
  static constexpr std::int16_t kRowPitch = 10;
  static constexpr std::int16_t kRowHeight = 10;

  [[nodiscard]] static constexpr std::int16_t
  RowTextY(std::uint8_t row) {
    return static_cast<std::int16_t>(kFirstRowTextY + row * kRowPitch);
  }

  [[nodiscard]] static constexpr std::int16_t
  RowBoundsY(std::uint8_t row) {
    return static_cast<std::int16_t>(RowTextY(row) - 1);
  }

  [[nodiscard]] static constexpr RectI16 RowDamage(std::uint8_t row,
                                                   std::int16_t width) {
    return {5, RowBoundsY(row), width, 11};
  }
};

} // namespace ui2
