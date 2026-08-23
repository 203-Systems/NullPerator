/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Song/UiSongView.h"

#include "UI2/Render/UiFrameRenderer.h"
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

RectI16 UiSongView::CellDamageRect(std::uint8_t track, std::uint8_t row) {
  if (track >= 8U || row >= 16U) return {};
  return {static_cast<std::int16_t>(kTrackX[track] - 3),
          static_cast<std::int16_t>(46 + row * 10), 17, 11};
}

RectI16 UiSongView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U) return {};
  return {5, static_cast<std::int16_t>(46 + row * 10), 213, 11};
}

RectI16 UiSongView::TrackHeaderDamageRect(std::uint8_t track) {
  if (track >= 8U) return {};
  return {static_cast<std::int16_t>(kTrackX[track] - 2), 36, 16, 10};
}

RectI16 UiSongView::BottomTrackDamageRect(std::uint8_t track) {
  if (track >= 8U) return {};
  return {static_cast<std::int16_t>(track * 30), 208, 30, 32};
}

RectI16 UiSongView::VuDamageRect(std::uint8_t channel) {
  if (channel >= 2U) return {};
  return {static_cast<std::int16_t>(219 + channel * 9), 47, 7, 153};
}

bool UiSongView::RequiresFullInvalidation(const UiSongViewData &previous,
                                          const UiSongViewData &current) {
  return previous.rowOffset != current.rowOffset ||
         previous.playing != current.playing ||
         previous.power != current.power;
}

void UiSongView::RenderDelta(const UiSongViewData &previous,
                             const UiSongViewData &current,
                             const UiFrameScene &currentScene,
                             UiIndexedSurface &surface,
                             const UiPalette &palette) {
  if (RequiresFullInvalidation(previous, current)) {
    UiFrameRenderer::RenderStatic(currentScene, surface, palette);
    return;
  }

  std::array<bool, 16> rowRendered{};
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };

  if (previous.name != current.name) render({59, 0, 132, 34});
  if (previous.elapsed != current.elapsed) render({190, 0, 50, 34});

  if (previous.editRow != current.editRow) {
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
    rowRendered[previous.editRow] = true;
    rowRendered[current.editRow] = true;
  }
  if (previous.editTrack != current.editTrack) {
    render(TrackHeaderDamageRect(previous.editTrack));
    render(TrackHeaderDamageRect(current.editTrack));
    if (!rowRendered[current.editRow]) {
      render(CellDamageRect(previous.editTrack, current.editRow));
      render(CellDamageRect(current.editTrack, current.editRow));
    }
  }

  std::uint16_t changedCells = 0;
  for (std::uint8_t row = 0; row < 16U; ++row) {
    for (std::uint8_t track = 0; track < 8U; ++track) {
      if (previous.rows[row][track] != current.rows[row][track]) {
        ++changedCells;
      }
    }
  }
  if (changedCells > 24U) {
    render({0, 34, 219, 174});
    rowRendered.fill(true);
  } else {
    for (std::uint8_t row = 0; row < 16U; ++row) {
      if (rowRendered[row]) continue;
      for (std::uint8_t track = 0; track < 8U; ++track) {
        if (previous.rows[row][track] != current.rows[row][track]) {
          render(CellDamageRect(track, row));
        }
      }
    }
  }

  for (std::uint8_t track = 0; track < 8U; ++track) {
    if (previous.playbackRows[track] != current.playbackRows[track]) {
      const std::int8_t previousRow = previous.playbackRows[track];
      const std::int8_t currentRow = current.playbackRows[track];
      if (previousRow >= 0 && previousRow < 16 &&
          !rowRendered[previousRow]) {
        render(CellDamageRect(track,
                              static_cast<std::uint8_t>(previousRow)));
      }
      if (currentRow >= 0 && currentRow < 16 &&
          !rowRendered[currentRow]) {
        render(CellDamageRect(track, static_cast<std::uint8_t>(currentRow)));
      }
    }
    if (previous.notes[track] != current.notes[track]) {
      render(BottomTrackDamageRect(track));
    }
  }

  for (std::uint8_t channel = 0; channel < 2U; ++channel) {
    if (previous.vuLevelTop[channel] != current.vuLevelTop[channel]) {
      render(VuDamageRect(channel));
    }
  }
}

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
      .power = data.power,
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
                 track == data.editTrack ? UiColorToken::CursorPrimary
                                         : UiColorToken::TextMuted);
  }

  const std::int16_t editY = static_cast<std::int16_t>(47 + data.editRow * 10);
  builder.Fill({5, editY, 213, 10}, UiColorToken::CursorRow);
  for (std::uint8_t row = 0; row < 16; ++row) {
    const std::int16_t y = static_cast<std::int16_t>(47 + row * 10);
    const auto rowLabel =
        HexByte(static_cast<std::uint8_t>(data.rowOffset + row));
    builder.Text(rowLabel.data(), 7, y,
                 row == data.editRow ? UiColorToken::CursorPrimary
                                     : UiColorToken::TextDim);
    for (std::uint8_t track = 0; track < 8; ++track) {
      const auto value = HexByte(data.rows[row][track]);
      const char *displayValue =
          data.rows[row][track] == 0xFFU ? "--" : value.data();
      const bool selected = row == data.editRow && track == data.editTrack;
      const bool playback = data.playing &&
                            data.playbackRows[track] ==
                                static_cast<std::int8_t>(row);
      if (selected) {
        builder.Selection(
            {static_cast<std::int16_t>(kTrackX[track] - 2),
             static_cast<std::int16_t>(y - 1), 15, 9},
            playback);
        builder.Text(displayValue, kTrackX[track], y, UiColorToken::CursorInk);
      } else {
        builder.Text(displayValue, kTrackX[track], y,
                     data.rows[row][track] == 0 ||
                             data.rows[row][track] == 0xFFU
                         ? UiColorToken::TextDim
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
