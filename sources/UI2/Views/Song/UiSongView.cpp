/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Song/UiSongView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Render/UiVuGradient.h"
#include "UI2/Views/Tracker/UiTrackerGridMetrics.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::int16_t, 8> kTrackX{28, 49, 70, 91,
                                              112, 133, 154, 175};
constexpr std::array<std::string_view, 2> kSongModes{"SONG", "LIVE"};

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

RectI16 ResolvedCursorRect(const UiSongViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiSongView::CursorTargetRect(data.editTrack, data.editRow);
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty()) return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
}

} // namespace

RectI16 UiSongView::CellDamageRect(std::uint8_t track, std::uint8_t row) {
  if (track >= 8U || row >= 16U) return {};
  return {static_cast<std::int16_t>(kTrackX[track] - 3),
          UiTrackerGridMetrics::RowBoundsY(row), 17, 11};
}

RectI16 UiSongView::CursorTargetRect(std::uint8_t track, std::uint8_t row) {
  if (track >= 8U || row >= 16U) return {};
  return {static_cast<std::int16_t>(kTrackX[track] - 2),
          UiTrackerGridMetrics::RowBoundsY(row), 15, 9};
}

RectI16 UiSongView::SelectionTargetRect(std::int16_t left, std::int16_t top,
                                        std::int16_t right,
                                        std::int16_t bottom,
                                        std::uint8_t rowOffset) {
  left = std::clamp<std::int16_t>(left, 0, 7);
  right = std::clamp<std::int16_t>(right, 0, 7);
  if (left > right)
    std::swap(left, right);
  if (top > bottom)
    std::swap(top, bottom);
  const std::int16_t firstVisible = rowOffset;
  const std::int16_t lastVisible = static_cast<std::int16_t>(rowOffset + 15U);
  top = std::max(top, firstVisible);
  bottom = std::min(bottom, lastVisible);
  if (top > bottom)
    return {};
  return Union(CursorTargetRect(static_cast<std::uint8_t>(left),
                                static_cast<std::uint8_t>(top - firstVisible)),
               CursorTargetRect(
                   static_cast<std::uint8_t>(right),
                   static_cast<std::uint8_t>(bottom - firstVisible)));
}

RectI16 UiSongView::PlaybackTickRect(std::uint8_t track, std::uint8_t row) {
  if (track >= 8U || row >= 16U) return {};
  return {static_cast<std::int16_t>(kTrackX[track] - 3),
          static_cast<std::int16_t>(UiTrackerGridMetrics::RowTextY(row) + 1),
          2, 5};
}

RectI16 UiSongView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U) return {};
  return UiTrackerGridMetrics::RowDamage(row, 213);
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
         previous.showVu != current.showVu ||
         previous.showBottom != current.showBottom ||
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

  if (previous.liveMode != current.liveMode) render({0, 0, 61, 34});
  if (previous.name != current.name) render({59, 0, 132, 34});
  if (previous.elapsed != current.elapsed) render({190, 0, 50, 34});

  const RectI16 previousCursor = ResolvedCursorRect(previous);
  const RectI16 currentCursor = ResolvedCursorRect(current);
  if (previous.selectionVisualRect != current.selectionVisualRect) {
    render(previous.selectionVisualRect);
    render(current.selectionVisualRect);
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
  }
  if (previousCursor != currentCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(previousCursor));
    render(ExpandedCursorDamage(currentCursor));
    render(CellDamageRect(previous.editTrack, previous.editRow));
    render(CellDamageRect(current.editTrack, current.editRow));
  }

  if (previous.editRow != current.editRow) {
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
    if (previous.editRow < rowRendered.size()) {
      rowRendered[previous.editRow] = true;
    }
    if (current.editRow < rowRendered.size()) {
      rowRendered[current.editRow] = true;
    }
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
  if (previous.adjustmentFocus != current.adjustmentFocus ||
      previous.modeFocus != current.modeFocus ||
      previous.liveMode != current.liveMode)
    render({0, 208, 240, 32});

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
  scene.bottomVisible = data.showBottom;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel top{
      .title = data.liveMode ? "LIVE" : "SONG",
      .meta = data.name,
      .elapsed = data.elapsed,
      .metaX = 61,
      .power = data.power,
  };
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Hidden};
  if (data.showBottom && data.adjustmentFocus) {
    bottom.kind = UiBottomBarKind::AdjustmentLegend;
  } else if (data.showBottom && data.modeFocus) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kSongModes;
    bottom.selector.current = data.liveMode ? 1U : 0U;
    bottom.selector.wrap = true;
  } else if (data.showBottom) {
    bottom.kind = UiBottomBarKind::TrackNotes;
  }
  if (bottom.kind == UiBottomBarKind::TrackNotes)
    bottom.trackNotes.notes = data.notes;
  if (bottom.kind == UiBottomBarKind::AdjustmentLegend)
    bottom.adjustment = {.fineStep = 1U, .coarseStep = 10U};
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  for (std::uint8_t track = 0; track < 8; ++track) {
    std::array<char, 3> label{'T', static_cast<char>('1' + track), 0};
    builder.Text(label.data(), kTrackX[track],
                 UiTrackerGridMetrics::kHeaderTextY,
                 track == data.editTrack ? UiColorToken::TextColored
                                         : UiColorToken::TextDim);
  }

  if (!data.selectionVisualRect.Empty()) {
    builder.RowHighlight(data.selectionVisualRect);
  } else {
    builder.RowHighlight({5, UiTrackerGridMetrics::RowBoundsY(data.editRow),
                          213, UiTrackerGridMetrics::kRowHeight});
  }
  const RectI16 cursorRect = ResolvedCursorRect(data);
  const RectI16 targetRect = CursorTargetRect(data.editTrack, data.editRow);
  for (std::uint8_t row = 0; row < 16; ++row) {
    const std::int16_t y = UiTrackerGridMetrics::RowTextY(row);
    const auto rowLabel =
        HexByte(static_cast<std::uint8_t>(data.rowOffset + row));
    builder.Text(rowLabel.data(), UiTrackerGridMetrics::kRowLabelX, y,
                 row == data.editRow ? UiColorToken::TextColored
                                     : UiColorToken::DerivedTextFaint);
    for (std::uint8_t track = 0; track < 8; ++track) {
      const auto value = HexByte(data.rows[row][track]);
      const char *displayValue =
          data.rows[row][track] == 0xFFU ? "--" : value.data();
      const bool target = row == data.editRow && track == data.editTrack;
      const bool playback = data.playing &&
                            data.playbackRows[track] ==
                                static_cast<std::int8_t>(row);
      builder.Text(displayValue, kTrackX[track], y,
                   data.rows[row][track] == 0 ||
                           data.rows[row][track] == 0xFFU
                       ? UiColorToken::DerivedTextFaint
                       : UiColorToken::TextNormal);
      if (playback && !(target && cursorRect == targetRect)) {
        builder.Fill(PlaybackTickRect(track, row),
                     UiColorToken::PlaybackActive);
      }
    }
  }

  bool cursorOverPlayback = false;
  if (!cursorRect.Empty() && data.playing) {
    for (std::uint8_t track = 0; track < 8; ++track) {
      const std::int8_t playbackRow = data.playbackRows[track];
      if (playbackRow >= 0 && playbackRow < 16 &&
          !Intersect(cursorRect, PlaybackTickRect(
                                     track, static_cast<std::uint8_t>(
                                                playbackRow)))
               .Empty()) {
        cursorOverPlayback = true;
        break;
      }
    }
  }
  builder.Selection(cursorRect, cursorOverPlayback);
  if (data.cursorInkVisible && data.editTrack < 8U && data.editRow < 16U) {
    const auto selectedValue = HexByte(data.rows[data.editRow][data.editTrack]);
    const char *displayValue = data.rows[data.editRow][data.editTrack] == 0xFFU
                                   ? "--"
                                   : selectedValue.data();
    builder.Text(displayValue, kTrackX[data.editTrack],
                 UiTrackerGridMetrics::RowTextY(data.editRow),
                 UiColorToken::TextHighlighted);
  }

  if (data.showVu) {
    if (!UiVuGradient::Configure(palette, 153)) {
      return UiBuildStatus::CommandOverflow;
    }
    for (std::uint8_t channel = 0; channel < 2; ++channel) {
      const std::int16_t x = static_cast<std::int16_t>(219 + channel * 9);
      builder.Fill({x, 47, 7, 153}, UiColorToken::DerivedVuTrack);
      const std::uint8_t level = data.vuLevelTop[channel];
      builder.VerticalPaletteRamp(
          {x, static_cast<std::int16_t>(47 + level), 7,
           static_cast<std::int16_t>(153 - level)},
          UiVuGradient::IndexAt(level));
    }
  }

  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
