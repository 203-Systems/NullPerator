/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Song/UiSongView.h"

#include "UI2/Render/UiVuGradient.h"

#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::int16_t, 8> kTrackX{28, 49, 70, 91,
                                              112, 133, 154, 175};

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

} // namespace

UiBuildStatus UiSongView::Build(const UiSongViewData &data,
                                UiPalette &palette, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  scene.bottomBackground = UiColorToken::SurfaceBarDeep;

  const UiTopBarModel top{
      .title = "SONG",
      .meta = data.name,
      .elapsed = data.elapsed,
      .metaX = 61,
      .power = data.playing ? UiPowerState::Playing
                            : UiPowerState::BatteryNormal,
  };
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::TrackNotes};
  bottom.trackNotes.notes = data.notes;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  for (std::uint8_t track = 0; track < 8; ++track) {
    std::array<char, 3> label{'T', static_cast<char>('1' + track), 0};
    builder.Text(label.data(), kTrackX[track], 38,
                 track == 0 ? UiColorToken::CursorPrimary
                            : UiColorToken::TextMuted);
  }

  const std::int16_t editY = static_cast<std::int16_t>(47 + data.editRow * 10);
  builder.Fill({5, editY, 213, 10}, UiColorToken::CursorRow);
  for (std::uint8_t row = 0; row < 16; ++row) {
    const std::int16_t y = static_cast<std::int16_t>(47 + row * 10);
    const auto rowLabel = HexByte(row);
    builder.Text(rowLabel.data(), 7, y,
                 row == data.editRow ? UiColorToken::CursorPrimary
                                     : UiColorToken::TextDim);
    for (std::uint8_t track = 0; track < 8; ++track) {
      const auto value = HexByte(data.rows[row][track]);
      const bool selected = row == data.editRow && track == data.editTrack;
      const bool playback = data.playing && data.playbackRows[track] == row;
      if (selected) {
        builder.Selection(
            {static_cast<std::int16_t>(kTrackX[track] - 2),
             static_cast<std::int16_t>(y - 1), 15, 9},
            playback);
        builder.Text(value.data(), kTrackX[track], y,
                     UiColorToken::CursorInk);
      } else {
        builder.Text(value.data(), kTrackX[track], y,
                     data.rows[row][track] == 0 ? UiColorToken::TextDim
                                                : UiColorToken::TextPrimary);
      }
      if (playback && !selected) {
        builder.Fill({static_cast<std::int16_t>(kTrackX[track] - 3),
                      static_cast<std::int16_t>(y + 1), 2, 5},
                     UiColorToken::PlaybackActive);
      }
    }
  }

  if (!UiVuGradient::Configure(palette, 153)) {
    return UiBuildStatus::CommandOverflow;
  }
  for (std::uint8_t channel = 0; channel < 2; ++channel) {
    const std::int16_t x = static_cast<std::int16_t>(219 + channel * 9);
    builder.Fill({x, 47, 7, 153}, UiColorToken::VuTrack);
    const std::uint8_t level = data.vuLevelTop[channel];
    builder.VerticalPaletteRamp(
        {x, static_cast<std::int16_t>(47 + level), 7,
         static_cast<std::int16_t>(153 - level)},
        UiVuGradient::IndexAt(level));
  }

  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
