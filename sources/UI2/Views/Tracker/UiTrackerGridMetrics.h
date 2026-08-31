/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"
#include "UI2/Text/UiFont5x7.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

namespace tracker_grid_detail {

template <std::size_t Count>
[[nodiscard]] constexpr std::array<std::int16_t, Count>
PackColumns(const std::array<std::uint8_t, Count> &characters,
            std::int16_t startX, std::int16_t gap) {
  std::array<std::int16_t, Count> result{};
  std::int16_t x = startX;
  for (std::size_t column = 0; column < Count; ++column) {
    result[column] = x;
    x = static_cast<std::int16_t>(
        x + UiFont5x7::TextWidth(characters[column]) + gap);
  }
  return result;
}

} // namespace tracker_grid_detail

// Song, Phrase and Table deliberately share one vertical rhythm. Keeping the
// metrics here prevents one tracker page from silently packing its 16 rows
// more tightly than the others.
struct UiTrackerGridMetrics {
  // Horizontal page bounds remain explicit so row-band damage, content and
  // stereo VU geometry cannot drift apart through unrelated magic numbers.
  static constexpr std::int16_t kRowBandLeftX = 6;
  static constexpr std::int16_t kGridRightWithVu = 218;
  static constexpr std::int16_t kGridRightFull = 235;
  static constexpr std::int16_t kVuFirstX = 218;
  static constexpr std::int16_t kVuChannelPitch = 9;
  static constexpr std::int16_t kVuChannelWidth = 7;

  // All tracker pages anchor hexadecimal row labels to this exact column.
  static constexpr std::int16_t kRowLabelX = 8;
  static constexpr std::int16_t kHeaderTextY = 38;
  // Two visible pixels separate the 5x7 header glyphs from row 00.
  static constexpr std::int16_t kFirstRowTextY = 48;
  static constexpr std::int16_t kRowPitch = 10;
  static constexpr std::int16_t kRowHeight = 10;

  // Tracker pages share the same content origin and clear space between
  // cells. Coordinates are derived from each page's real character widths:
  // Phrase and Table have opposite 4/3-character cadences, so reusing one
  // page's absolute anchors makes the other page alternate between cramped
  // and oversized gaps.
  static constexpr std::int16_t kContentStartX = 29;
  static constexpr std::int16_t kColumnGap = 12;

  static constexpr std::array<std::uint8_t, 8> kSongColumnCharacters{
      2, 2, 2, 2, 2, 2, 2, 2};
  static constexpr std::array<std::uint8_t, 6> kPhraseColumnCharacters{
      4, 3, 3, 4, 3, 4};
  static constexpr std::array<std::uint8_t, 6> kTableColumnCharacters{
      3, 4, 3, 4, 3, 4};

  static constexpr auto kSongTrackX = tracker_grid_detail::PackColumns(
      kSongColumnCharacters, kContentStartX, kColumnGap);
  static constexpr std::array<std::int16_t, 2> kChainColumnX{
      kSongTrackX[0], kSongTrackX[1]};
  static constexpr auto kPhraseColumnX =
      tracker_grid_detail::PackColumns(kPhraseColumnCharacters, kContentStartX,
                                       kColumnGap);
  static constexpr auto kTableColumnX = tracker_grid_detail::PackColumns(
      kTableColumnCharacters, kContentStartX, kColumnGap);

  static_assert(kSongTrackX.back() + UiFont5x7::TextWidth(2) <
                    kGridRightWithVu,
                "Song tracks must leave room for the dual VU meter");
  static_assert(kPhraseColumnX.back() + UiFont5x7::TextWidth(4) + 2 <= 240,
                "Phrase cursor must remain on screen");
  static_assert(kTableColumnX.back() + UiFont5x7::TextWidth(4) + 2 <= 240,
                "Table cursor must remain on screen");

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

  [[nodiscard]] static constexpr RectI16
  RowHighlightRect(std::uint8_t row, std::int16_t rightExclusive) {
    return {kRowBandLeftX, RowHighlightY(row),
            static_cast<std::int16_t>(rightExclusive - kRowBandLeftX),
            kRowHeight};
  }

  [[nodiscard]] static constexpr RectI16
  RowDamage(std::uint8_t row, std::int16_t rightExclusive) {
    const RectI16 highlight = RowHighlightRect(row, rightExclusive);
    return {highlight.x, highlight.y, highlight.width, 12};
  }

  [[nodiscard]] static constexpr std::int16_t VuX(std::uint8_t channel) {
    return static_cast<std::int16_t>(kVuFirstX + channel * kVuChannelPitch);
  }
};

} // namespace ui2
