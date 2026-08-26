/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <array>
#include <cstdint>

namespace ui2 {

// Song, Phrase and Table deliberately share one vertical rhythm. Keeping the
// metrics here prevents one tracker page from silently packing its 16 rows
// more tightly than the others.
struct UiTrackerGridMetrics {
  // All tracker pages anchor hexadecimal row labels to this exact column.
  static constexpr std::int16_t kRowLabelX = 7;
  static constexpr std::int16_t kHeaderTextY = 38;
  // Two visible pixels separate the 5x7 header glyphs from row 00.
  static constexpr std::int16_t kFirstRowTextY = 48;
  static constexpr std::int16_t kRowPitch = 10;
  static constexpr std::int16_t kRowHeight = 10;

  // Horizontal anchors are shared here for the same reason as the vertical
  // rhythm above. Phrase defines the approved six-column cadence; Table uses
  // it verbatim so FX1/FX2/FX3 never drift from the corresponding Phrase
  // groups. Chain shares Song's first two track anchors.
  static constexpr std::array<std::int16_t, 8> kSongTrackX{
      28, 49, 70, 91, 112, 133, 154, 175};
  static constexpr std::array<std::int16_t, 2> kChainColumnX{28, 49};
  static constexpr std::array<std::int16_t, 6> kPhraseColumnX{
      28, 61, 88, 115, 148, 175};
  static constexpr const auto &kTableColumnX = kPhraseColumnX;

  [[nodiscard]] static constexpr std::int16_t
  RowTextY(std::uint8_t row) {
    return static_cast<std::int16_t>(kFirstRowTextY + row * kRowPitch);
  }

  [[nodiscard]] static constexpr std::int16_t
  RowBoundsY(std::uint8_t row) {
    return static_cast<std::int16_t>(RowTextY(row) - 1);
  }

  // The low-contrast row band has one pixel of breathing room above the
  // cursor bubble. Keep it separate so tuning the row background does not
  // move the animated cell cursor or its glyph.
  [[nodiscard]] static constexpr std::int16_t
  RowHighlightY(std::uint8_t row) {
    return static_cast<std::int16_t>(RowTextY(row) - 2);
  }

  [[nodiscard]] static constexpr RectI16 RowDamage(std::uint8_t row,
                                                   std::int16_t width) {
    return {5, RowHighlightY(row), width, 12};
  }
};

} // namespace ui2
