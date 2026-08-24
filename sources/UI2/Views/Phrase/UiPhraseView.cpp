/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Phrase/UiPhraseView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"
#include "UI2/Views/Tracker/UiTrackerGridMetrics.h"

#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::int16_t, 6> kColumnX{28, 61, 88, 115, 148, 175};

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

bool IsDimValue(std::string_view value) {
  return value == "----" || value == "0000" || value == "I--" || value == "---";
}

UiColorToken HeaderColor(UiPhraseHeader active, UiPhraseHeader candidate) {
  return active == candidate ? UiColorToken::TextColored
                             : UiColorToken::TextDim;
}

RectI16 ResolvedCursorRect(const UiPhraseViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiPhraseView::CursorTargetRect(data);
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

bool BottomEqual(const UiBottomBarModel &left, const UiBottomBarModel &right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind == UiBottomBarKind::Hidden)
    return true;
  if (left.kind == UiBottomBarKind::TrackNotes) {
    return left.trackNotes.notes == right.trackNotes.notes &&
           left.trackNotes.selectedTrack == right.trackNotes.selectedTrack &&
           left.trackNotes.selectedNote == right.trackNotes.selectedNote &&
           left.trackNotes.trackSelectionRect ==
               right.trackNotes.trackSelectionRect &&
           left.trackNotes.trackSelectionOverride ==
               right.trackNotes.trackSelectionOverride &&
           left.trackNotes.trackInkVisible == right.trackNotes.trackInkVisible;
  }
  if (left.kind != UiBottomBarKind::Context)
    return false;
  const auto equalSegments = [](const auto &a, const auto &b,
                                std::uint8_t count) {
    for (std::uint8_t index = 0; index < count; ++index) {
      if (a[index].text != b[index].text || a[index].color != b[index].color ||
          a[index].x != b[index].x) {
        return false;
      }
    }
    return true;
  };
  return left.context.firstLineCount == right.context.firstLineCount &&
         left.context.secondLineCount == right.context.secondLineCount &&
         equalSegments(left.context.firstLine, right.context.firstLine,
                       left.context.firstLineCount) &&
         equalSegments(left.context.secondLine, right.context.secondLine,
                       left.context.secondLineCount);
}

} // namespace

RectI16 UiPhraseView::CursorTargetRect(const UiPhraseViewData &data) {
  if (data.editRow >= 16U || data.editColumn >= kColumnX.size())
    return {};
  const std::string_view value = data.rows[data.editRow][data.editColumn];
  return {static_cast<std::int16_t>(kColumnX[data.editColumn] - 2),
          UiTrackerGridMetrics::RowBoundsY(data.editRow),
          static_cast<std::int16_t>(UiFont5x7::TextWidth(value.size()) + 4), 9};
}

RectI16 UiPhraseView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return UiTrackerGridMetrics::RowDamage(row, 230);
}

bool UiPhraseView::RequiresFullInvalidation(const UiPhraseViewData &previous,
                                            const UiPhraseViewData &current) {
  return previous.rowOffset != current.rowOffset ||
         previous.numberFocus != current.numberFocus;
}

void UiPhraseView::RenderDelta(const UiPhraseViewData &previous,
                               const UiPhraseViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  if (RequiresFullInvalidation(previous, current)) {
    UiFrameRenderer::RenderStatic(currentScene, surface, palette);
    return;
  }
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };

  if (previous.number != current.number ||
      previous.topMetaVisualRect != current.topMetaVisualRect ||
      previous.topMetaVisualOverride != current.topMetaVisualOverride ||
      previous.topMetaInkVisible != current.topMetaInkVisible) {
    render({72, 0, 40, 34});
  }
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (previous.activeHeader != current.activeHeader) {
    render({24, 34, 171, 13});
  }

  std::array<bool, 16> rowRendered{};
  const RectI16 previousCursor = ResolvedCursorRect(previous);
  const RectI16 currentCursor = ResolvedCursorRect(current);
  if (previousCursor != currentCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(previousCursor));
    render(ExpandedCursorDamage(currentCursor));
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
    if (previous.editRow < rowRendered.size()) {
      rowRendered[previous.editRow] = true;
    }
    if (current.editRow < rowRendered.size()) {
      rowRendered[current.editRow] = true;
    }
  } else if (previous.editRow != current.editRow) {
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
    if (previous.editRow < rowRendered.size()) {
      rowRendered[previous.editRow] = true;
    }
    if (current.editRow < rowRendered.size()) {
      rowRendered[current.editRow] = true;
    }
  }

  std::uint8_t changedRows = 0;
  for (std::uint8_t row = 0; row < 16U; ++row) {
    if (previous.rows[row] != current.rows[row])
      ++changedRows;
  }
  if (changedRows > 6U) {
    render({0, 34, 240, 174});
  } else {
    for (std::uint8_t row = 0; row < 16U; ++row) {
      if (!rowRendered[row] && previous.rows[row] != current.rows[row]) {
        render(RowDamageRect(row));
      }
    }
  }

  if (previous.trackNotes != current.trackNotes ||
      previous.selectedTrack != current.selectedTrack ||
      previous.bottomTrackVisualRect != current.bottomTrackVisualRect ||
      previous.bottomTrackVisualOverride != current.bottomTrackVisualOverride ||
      previous.bottomTrackInkVisible != current.bottomTrackInkVisible ||
      !BottomEqual(previous.cursorBottom, current.cursorBottom)) {
    render({0, 208, 240, 32});
  }
}

UiBuildStatus UiPhraseView::Build(const UiPhraseViewData &data, UiPalette &,
                                  UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel pageTop{
      .title = "PHRASE",
      .meta = data.number,
      .elapsed = data.elapsed,
      .metaX = 85,
      .power = data.power,
      .metaSelectionRect = data.topMetaVisualRect,
      .metaSelectionOverride = data.topMetaVisualOverride,
      .metaInkVisible = data.topMetaInkVisible,
  };
  const UiBottomBarModel pageBottom{.kind = UiBottomBarKind::Hidden};
  UiTrackNotesModel enterTracks;
  enterTracks.notes = data.trackNotes;
  enterTracks.selectedTrack = data.selectedTrack;
  enterTracks.trackSelectionRect = data.bottomTrackVisualRect;
  enterTracks.trackSelectionOverride = data.bottomTrackVisualOverride;
  enterTracks.trackInkVisible = data.bottomTrackInkVisible;
  const UiBarInputs barInputs{
      .pageTop = pageTop,
      .pageDefault = pageBottom,
      .cursorContext = data.numberFocus ? nullptr : &data.cursorBottom,
      .enterHeldTracks = &enterTracks,
      .enterHeldNumber = data.numberFocus,
  };
  const UiResolvedChrome chrome = UiBarResolver::Resolve(barInputs);
  const UiBuildStatus topStatus =
      UiChromeRenderer::BuildTop(chrome.top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  scene.bottomVisible = chrome.bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(chrome.bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("NOTE", kColumnX[0], UiTrackerGridMetrics::kHeaderTextY,
               HeaderColor(data.activeHeader, UiPhraseHeader::Note));
  builder.Text("INS", kColumnX[1], UiTrackerGridMetrics::kHeaderTextY,
               HeaderColor(data.activeHeader, UiPhraseHeader::Instrument));
  builder.Text("FX1", kColumnX[2], UiTrackerGridMetrics::kHeaderTextY,
               HeaderColor(data.activeHeader, UiPhraseHeader::Fx1));
  builder.Text("FX2", kColumnX[4], UiTrackerGridMetrics::kHeaderTextY,
               HeaderColor(data.activeHeader, UiPhraseHeader::Fx2));

  if (!data.numberFocus && data.editRow < 16U) {
    builder.Fill(
        {5, UiTrackerGridMetrics::RowTextY(data.editRow), 230,
         UiTrackerGridMetrics::kRowHeight},
        UiColorToken::CursorRow);
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const std::int16_t y = UiTrackerGridMetrics::RowTextY(row);
    const auto rowLabel =
        HexByte(static_cast<std::uint8_t>(data.rowOffset + row));
    builder.Text(rowLabel.data(), 7, y,
                 !data.numberFocus && row == data.editRow
                     ? UiColorToken::TextColored
                     : UiColorToken::DerivedTextFaint);
    for (std::uint8_t column = 0; column < kColumnX.size(); ++column) {
      const std::string_view value = data.rows[row][column];
      builder.Text(value, kColumnX[column], y,
                   IsDimValue(value) ? UiColorToken::DerivedTextFaint
                                     : UiColorToken::TextNormal);
    }
  }

  if (!data.numberFocus) {
    const RectI16 cursor = ResolvedCursorRect(data);
    builder.Selection(cursor);
    if (data.cursorInkVisible && data.editRow < 16U &&
        data.editColumn < kColumnX.size()) {
      builder.Text(data.rows[data.editRow][data.editColumn],
                   kColumnX[data.editColumn],
                   UiTrackerGridMetrics::RowTextY(data.editRow),
                   UiColorToken::TextHighlighted);
    }
  }

  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
