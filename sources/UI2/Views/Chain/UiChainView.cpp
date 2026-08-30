/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Chain/UiChainView.h"

#include "Application/UI2/Ui2ChainTranspose.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Render/UiVuGradient.h"
#include "UI2/Views/Tracker/UiTrackerGridMetrics.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

constexpr const auto &kColumnX = UiTrackerGridMetrics::kChainColumnX;

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

RectI16 ResolvedCursorRect(const UiChainViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiChainView::CursorTargetRect(data);
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty())
    return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
}

} // namespace

RectI16 UiChainView::CursorTargetRect(const UiChainViewData &data) {
  if (data.editRow >= 16U || data.editColumn >= 2U)
    return {};
  return {static_cast<std::int16_t>(kColumnX[data.editColumn] - 2),
          UiTrackerGridMetrics::RowBoundsY(data.editRow),
          static_cast<std::int16_t>(data.editColumn == 0U ? 15 : 21), 9};
}

RectI16 UiChainView::SelectionTargetRect(std::int16_t left, std::int16_t top,
                                         std::int16_t right,
                                         std::int16_t bottom) {
  left = std::clamp<std::int16_t>(left, 0, 1);
  right = std::clamp<std::int16_t>(right, 0, 1);
  top = std::clamp<std::int16_t>(top, 0, 15);
  bottom = std::clamp<std::int16_t>(bottom, 0, 15);
  if (left > right)
    std::swap(left, right);
  if (top > bottom)
    std::swap(top, bottom);
  const auto cell = [](std::int16_t column, std::int16_t row) {
    return RectI16{static_cast<std::int16_t>(kColumnX[column] - 2),
                   UiTrackerGridMetrics::RowBoundsY(row),
                   static_cast<std::int16_t>(column == 0 ? 15 : 21), 9};
  };
  return Union(cell(left, top), cell(right, bottom));
}

RectI16 UiChainView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return UiTrackerGridMetrics::RowDamage(row, 213);
}

RectI16 UiChainView::PlaybackTickRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {static_cast<std::int16_t>(kColumnX[0] - 3),
          static_cast<std::int16_t>(UiTrackerGridMetrics::RowTextY(row) + 1),
          2, 5};
}

RectI16 UiChainView::VuDamageRect(std::uint8_t side) {
  if (side >= 2U)
    return {};
  return {static_cast<std::int16_t>(219 + side * 9), kMeterTop, 7,
          kMeterHeight};
}

void UiChainView::RenderDelta(const UiChainViewData &previous,
                              const UiChainViewData &current,
                              const UiFrameScene &currentScene,
                              UiIndexedSurface &surface,
                              const UiPalette &palette) {
  if (previous.numberFocus != current.numberFocus) {
    UiFrameRenderer::RenderStatic(currentScene, surface, palette);
    return;
  }
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.number != current.number ||
      previous.topMetaVisualRect != current.topMetaVisualRect ||
      previous.topMetaVisualOverride != current.topMetaVisualOverride ||
      previous.topMetaInkVisible != current.topMetaInkVisible)
    render({56, 0, 40, 34});
  if (previous.power != current.power || previous.elapsed != current.elapsed ||
      previous.navCursor != current.navCursor)
    render({184, 0, 56, 34});

  if (previous.editColumn != current.editColumn)
    render({28, 34, 64, 14});
  if (previous.selectionVisualRect != current.selectionVisualRect) {
    render(previous.selectionVisualRect);
    render(current.selectionVisualRect);
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
  }
  if (previous.playbackRow != current.playbackRow) {
    if (previous.playbackRow >= 0 && previous.playbackRow < 16)
      render(RowDamageRect(static_cast<std::uint8_t>(previous.playbackRow)));
    if (current.playbackRow >= 0 && current.playbackRow < 16)
      render(RowDamageRect(static_cast<std::uint8_t>(current.playbackRow)));
  }
  if (!current.numberFocus) {
    const RectI16 oldCursor = ResolvedCursorRect(previous);
    const RectI16 newCursor = ResolvedCursorRect(current);
    if (oldCursor != newCursor ||
        previous.cursorInkVisible != current.cursorInkVisible) {
      render(ExpandedCursorDamage(oldCursor));
      render(ExpandedCursorDamage(newCursor));
      render(RowDamageRect(previous.editRow));
      render(RowDamageRect(current.editRow));
    }
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    if (previous.phrases[row] != current.phrases[row] ||
        previous.transposes[row] != current.transposes[row]) {
      render(RowDamageRect(row));
    }
  }
  if (previous.trackNotes != current.trackNotes ||
      previous.selectedTrack != current.selectedTrack ||
      previous.bottomTrackVisualRect != current.bottomTrackVisualRect ||
      previous.bottomTrackVisualOverride != current.bottomTrackVisualOverride ||
      previous.bottomTrackInkVisible != current.bottomTrackInkVisible ||
      previous.adjustmentFocus != current.adjustmentFocus ||
      previous.selectionActive != current.selectionActive) {
    render({0, 208, 240, 32});
  }
  for (std::uint8_t side = 0; side < 2U; ++side) {
    if (previous.vuLevelTop[side] != current.vuLevelTop[side]) {
      render(VuDamageRect(side));
    }
  }
}

UiBuildStatus UiChainView::Build(const UiChainViewData &data,
                                 UiPalette &palette, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel top{
      .title = "CHAIN",
      .meta = data.number,
      .elapsed = data.elapsed,
      .power = data.power,
      .navTarget = UiNavTarget::Chain,
      .navCursor = data.navCursor,
      .metaSelectionRect = data.topMetaVisualRect,
      .metaSelectionOverride = data.topMetaVisualOverride,
      .metaInkVisible = data.topMetaInkVisible,
  };
  UiBottomBarModel pageBottom{.kind = UiBottomBarKind::TrackNotes};
  pageBottom.trackNotes.notes = data.trackNotes;
  UiTrackNotesModel editTracks;
  editTracks.notes = data.trackNotes;
  editTracks.selectedTrack = data.selectedTrack;
  editTracks.trackSelectionRect = data.bottomTrackVisualRect;
  editTracks.trackSelectionOverride = data.bottomTrackVisualOverride;
  editTracks.trackInkVisible = data.bottomTrackInkVisible;
  const UiAdjustmentLegendModel adjustment{
      .fineStep = 1U,
      .coarseStep = 10U,
      .coarseLabel = data.editColumn == 1U ? "OCT" : "",
  };
  const UiBarInputs barInputs{
      .pageTop = top,
      .pageDefault = pageBottom,
      .editHeldTracks = &editTracks,
      .enterHeldAdjustment = data.adjustmentFocus ? &adjustment : nullptr,
      .selectionActive = data.selectionActive,
      .editHeldNumber = data.numberFocus,
  };
  const UiResolvedChrome chrome = UiBarResolver::Resolve(barInputs);
  const UiBuildStatus topStatus =
      UiChromeRenderer::BuildTop(chrome.top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(chrome.bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  if (!UiVuGradient::Configure(palette, kMeterHeight)) {
    return UiBuildStatus::CommandOverflow;
  }
  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("PH", kColumnX[0], UiTrackerGridMetrics::kHeaderTextY,
               !data.numberFocus && data.editColumn == 0U
                   ? UiColorToken::TextColored
                   : UiColorToken::TextDim);
  builder.Text("TR", kColumnX[1], UiTrackerGridMetrics::kHeaderTextY,
               !data.numberFocus && data.editColumn == 1U
                   ? UiColorToken::TextColored
                   : UiColorToken::TextDim);
  const RectI16 cursor = ResolvedCursorRect(data);
  if (!data.numberFocus && !data.selectionVisualRect.Empty()) {
    builder.RowHighlight(data.selectionVisualRect);
  } else if (!data.numberFocus && data.editRow < 16U) {
    builder.RowHighlight({5, UiTrackerGridMetrics::RowHighlightY(data.editRow),
                          213, UiTrackerGridMetrics::kRowHeight});
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const std::int16_t y = UiTrackerGridMetrics::RowTextY(row);
    const auto rowText = HexByte(row);
    builder.Text(rowText.data(), UiTrackerGridMetrics::kRowLabelX, y,
                 !data.numberFocus && row == data.editRow
                     ? UiColorToken::TextColored
                     : UiColorToken::DerivedTextFaint);
    const auto phrase = HexByte(data.phrases[row]);
    const char *phraseText = data.phrases[row] == 0xFFU ? "--" : phrase.data();
    builder.Text(phraseText, kColumnX[0], y,
                 data.phrases[row] == 0xFFU ? UiColorToken::DerivedTextFaint
                                            : UiColorToken::TextNormal);
    const auto transpose = Ui2ChainTranspose::Format(data.transposes[row]);
    const bool rowEmpty = data.phrases[row] == 0xFFU;
    builder.Text(rowEmpty ? "---" : transpose.data(), kColumnX[1], y,
                 rowEmpty ? UiColorToken::DerivedTextFaint
                          : data.transposes[row] == 0U
                                ? UiColorToken::TextDim
                                : UiColorToken::TextNormal);
    if (data.playbackRow == static_cast<std::int8_t>(row))
      builder.Fill(PlaybackTickRect(row), UiColorToken::PlaybackActive);
  }
  if (!data.numberFocus) {
    const bool cursorOverPlayback =
        data.playbackRow >= 0 && data.playbackRow < 16 &&
        !Intersect(cursor,
                   PlaybackTickRect(
                       static_cast<std::uint8_t>(data.playbackRow)))
             .Empty();
    builder.Selection(cursor, cursorOverPlayback);
    if (data.cursorInkVisible && data.editRow < 16U &&
        data.editColumn < 2U) {
      const std::uint8_t value = data.editColumn == 0U
                                     ? data.phrases[data.editRow]
                                     : data.transposes[data.editRow];
      const auto phraseText = HexByte(value);
      const auto transposeText = Ui2ChainTranspose::Format(value);
      const char *display = data.editColumn == 0U
                                ? (value == 0xFFU ? "--" : phraseText.data())
                                : (data.phrases[data.editRow] == 0xFFU
                                       ? "---"
                                       : transposeText.data());
      builder.Text(display, kColumnX[data.editColumn],
                   UiTrackerGridMetrics::RowTextY(data.editRow),
                   UiColorToken::TextHighlighted);
    }
  }
  for (std::uint8_t side = 0; side < 2U; ++side) {
    const RectI16 meter = VuDamageRect(side);
    builder.Fill(meter, UiColorToken::DerivedVuTrack);
    const std::uint8_t level =
        std::min<std::uint8_t>(data.vuLevelTop[side], kMeterHeight);
    builder.VerticalPaletteRamp(
        {meter.x, static_cast<std::int16_t>(meter.y + level), meter.width,
         static_cast<std::int16_t>(meter.height - level)},
        UiVuGradient::IndexAt(level));
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
