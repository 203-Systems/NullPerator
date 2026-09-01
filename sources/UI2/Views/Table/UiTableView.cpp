/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Table/UiTableView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"
#include "UI2/Views/Tracker/UiTrackerGridMetrics.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

constexpr const auto &kColumnX = UiTrackerGridMetrics::kTableColumnX;
constexpr const auto &kColumnCharacters =
    UiTrackerGridMetrics::kTableColumnCharacters;

bool IsParameterColumn(std::uint8_t column) { return (column & 1U) != 0U; }

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

UiColorToken HeaderColor(UiTableHeader active, UiTableHeader candidate) {
  return active == candidate ? UiColorToken::TextColored
                             : UiColorToken::TextDim;
}

RectI16 ResolvedCursorRect(const UiTableViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiTableView::CursorTargetRect(data);
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

bool ContextEqual(const UiBottomBarModel &left, const UiBottomBarModel &right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind == UiBottomBarKind::Hidden)
    return true;
  if (left.kind != UiBottomBarKind::Context)
    return false;
  if (left.context.firstLineCount != right.context.firstLineCount ||
      left.context.secondLineCount != right.context.secondLineCount) {
    return false;
  }
  const auto equal = [](const auto &a, const auto &b, std::uint8_t count) {
    for (std::uint8_t index = 0; index < count; ++index) {
      if (a[index].text != b[index].text || a[index].color != b[index].color ||
          a[index].x != b[index].x) {
        return false;
      }
    }
    return true;
  };
  return equal(left.context.firstLine, right.context.firstLine,
               left.context.firstLineCount) &&
         equal(left.context.secondLine, right.context.secondLine,
               left.context.secondLineCount);
}

} // namespace

RectI16 UiTableView::CursorTargetRect(const UiTableViewData &data) {
  if (data.editRow >= 16U || data.editColumn >= kColumnX.size())
    return {};
  const std::string_view value = data.rows[data.editRow][data.editColumn];
  if (data.enterDigitFocus && IsParameterColumn(data.editColumn) &&
      !value.empty()) {
    const std::uint8_t digit = std::min<std::uint8_t>(
        data.editDigit, static_cast<std::uint8_t>(value.size() - 1U));
    return {static_cast<std::int16_t>(
                kColumnX[data.editColumn] + digit * UiFont5x7::kAdvance - 2),
            UiTrackerGridMetrics::RowBoundsY(data.editRow),
            static_cast<std::int16_t>(UiFont5x7::kGlyphWidth + 4), 9};
  }
  return {static_cast<std::int16_t>(kColumnX[data.editColumn] - 2),
          UiTrackerGridMetrics::RowBoundsY(data.editRow),
          static_cast<std::int16_t>(UiFont5x7::TextWidth(value.size()) + 4), 9};
}

RectI16 UiTableView::SelectionTargetRect(std::int16_t left, std::int16_t top,
                                         std::int16_t right,
                                         std::int16_t bottom) {
  left = std::clamp<std::int16_t>(left, 0, 5);
  right = std::clamp<std::int16_t>(right, 0, 5);
  top = std::clamp<std::int16_t>(top, 0, 15);
  bottom = std::clamp<std::int16_t>(bottom, 0, 15);
  if (left > right)
    std::swap(left, right);
  if (top > bottom)
    std::swap(top, bottom);
  const auto cell = [](std::int16_t column, std::int16_t row) {
    return RectI16{
        static_cast<std::int16_t>(kColumnX[column] - 2),
        UiTrackerGridMetrics::RowBoundsY(row),
        static_cast<std::int16_t>(
            UiFont5x7::TextWidth(kColumnCharacters[column]) + 4),
        9};
  };
  return Union(cell(left, top), cell(right, bottom));
}

RectI16 UiTableView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return UiTrackerGridMetrics::RowDamage(
      row, UiTrackerGridMetrics::kGridRightFull);
}

RectI16 UiTableView::PlaybackTickRect(std::uint8_t group,
                                      std::uint8_t row) {
  if (group >= 3U || row >= 16U)
    return {};
  return {static_cast<std::int16_t>(kColumnX[group * 2U] - 3),
          static_cast<std::int16_t>(UiTrackerGridMetrics::RowTextY(row) + 1),
          2, 5};
}

bool UiTableView::RequiresFullInvalidation(const UiTableViewData &previous,
                                           const UiTableViewData &current) {
  return previous.rowOffset != current.rowOffset ||
         previous.numberFocus != current.numberFocus;
}

void UiTableView::RenderDelta(const UiTableViewData &previous,
                              const UiTableViewData &current,
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
    render({64, 0, 48, 34});
  }
  if (previous.power != current.power || previous.elapsed != current.elapsed ||
      previous.navCursor != current.navCursor) {
    render({184, 0, 56, 34});
  }
  if (previous.activeHeader != current.activeHeader) {
    render({25, 34, 185, 14});
  }

  std::array<bool, 16> rowRendered{};
  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (previous.selectionVisualRect != current.selectionVisualRect) {
    render(previous.selectionVisualRect);
    render(current.selectionVisualRect);
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
  }
  const auto renderPlaybackChanges = [&](const auto &previousRows,
                                         const auto &currentRows) {
    for (std::uint8_t group = 0U; group < currentRows.size(); ++group) {
      if (previousRows[group] == currentRows[group])
        continue;
      if (previousRows[group] >= 0 && previousRows[group] < 16)
        render(RowDamageRect(
            static_cast<std::uint8_t>(previousRows[group])));
      if (currentRows[group] >= 0 && currentRows[group] < 16)
        render(RowDamageRect(static_cast<std::uint8_t>(currentRows[group])));
    }
  };
  renderPlaybackChanges(previous.playbackRows, current.playbackRows);
  renderPlaybackChanges(previous.automationPlaybackRows,
                        current.automationPlaybackRows);
  if (previous.selectedTrackMuted != current.selectedTrackMuted) {
    for (std::uint8_t group = 0U; group < current.playbackRows.size(); ++group) {
      const std::array<std::int8_t, 4> rows{
          previous.playbackRows[group], previous.automationPlaybackRows[group],
          current.playbackRows[group], current.automationPlaybackRows[group]};
      for (const std::int8_t row : rows) {
        if (row >= 0 && row < 16)
          render(RowDamageRect(static_cast<std::uint8_t>(row)));
      }
    }
  }
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
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
      previous.adjustmentFocus != current.adjustmentFocus ||
      previous.selectionActive != current.selectionActive ||
      previous.selectionNextExpansionAll !=
          current.selectionNextExpansionAll ||
      previous.clipboardReady != current.clipboardReady ||
      previous.clipboardPasted != current.clipboardPasted ||
      previous.clipboardWidth != current.clipboardWidth ||
      previous.clipboardHeight != current.clipboardHeight ||
      !ContextEqual(previous.cursorBottom, current.cursorBottom)) {
    render({0, 208, 240, 32});
  }
}

UiBuildStatus UiTableView::Build(const UiTableViewData &data, UiPalette &,
                                 UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel pageTop{
      .title = "TABLE",
      .meta = data.number,
      .elapsed = data.elapsed,
      .power = data.power,
      .navTarget = !data.number.empty() && data.number.front() == 'I'
                       ? UiNavTarget::InstrumentTable
                       : UiNavTarget::PhraseTable,
      .navCursor = data.navCursor,
      .metaSelectionRect = data.topMetaVisualRect,
      .metaSelectionOverride = data.topMetaVisualOverride,
      .metaInkVisible = data.topMetaInkVisible,
  };
  UiBottomBarModel pageBottom{.kind = UiBottomBarKind::TrackNotes};
  pageBottom.trackNotes.notes = data.trackNotes;
  UiTrackNotesModel tracks;
  tracks.notes = data.trackNotes;
  tracks.selectedTrack = data.selectedTrack;
  tracks.trackSelectionRect = data.bottomTrackVisualRect;
  tracks.trackSelectionOverride = data.bottomTrackVisualOverride;
  tracks.trackInkVisible = data.bottomTrackInkVisible;
  const UiBottomBarModel *cursorContext =
      !data.numberFocus && data.cursorBottom.kind != UiBottomBarKind::Hidden
          ? &data.cursorBottom
          : nullptr;
  const UiBarInputs inputs{
      .pageTop = pageTop,
      .pageDefault = pageBottom,
      .cursorContext = cursorContext,
      .enterHeldTracks = &tracks,
      // Table values are command-specific. Their help remains more useful
      // than a generic +/- legend; digit focus still appears in the cell.
      .enterHeldAdjustment = nullptr,
      .selectionActive = data.selectionActive,
      .selectionNextExpansionAll = data.selectionNextExpansionAll,
      .clipboardReady = data.clipboardReady,
      .clipboardWidth = data.clipboardWidth,
      .clipboardHeight = data.clipboardHeight,
      .clipboardPasted = data.clipboardPasted,
      .enterHeldNumber = data.numberFocus,
  };
  const UiResolvedChrome chrome = UiBarResolver::Resolve(inputs);
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
  constexpr std::array<std::string_view, 3> headers{"FX1", "FX2", "FX3"};
  constexpr std::array<UiTableHeader, 3> headerKinds{
      UiTableHeader::Fx1, UiTableHeader::Fx2, UiTableHeader::Fx3};
  for (std::uint8_t group = 0; group < headers.size(); ++group) {
    builder.Text(headers[group], kColumnX[group * 2U],
                 UiTrackerGridMetrics::kHeaderTextY,
                 HeaderColor(data.activeHeader, headerKinds[group]));
  }
  if (!data.numberFocus && !data.selectionVisualRect.Empty()) {
    builder.SelectionHighlight(data.selectionVisualRect);
  } else if (!data.numberFocus && data.editRow < 16U) {
    builder.RowHighlight(UiTrackerGridMetrics::RowHighlightRect(
        data.editRow, UiTrackerGridMetrics::kGridRightFull));
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const std::int16_t y = UiTrackerGridMetrics::RowTextY(row);
    const auto label = HexByte(static_cast<std::uint8_t>(data.rowOffset + row));
    builder.Text(label.data(), UiTrackerGridMetrics::kRowLabelX, y,
                 !data.numberFocus && row == data.editRow
                     ? UiColorToken::TextColored
                     : UiColorToken::DerivedTextFaint);
    for (std::uint8_t column = 0; column < kColumnX.size(); ++column) {
      const std::string_view value = data.rows[row][column];
      UiColorToken color = UiColorToken::TextNormal;
      if ((column & 1U) != 0U && value == "0000") {
        color = UiColorToken::DerivedTextFaint;
      } else if ((column & 1U) == 0U && value == "---") {
        color = UiColorToken::DerivedTextFaint;
      }
      builder.Text(value, kColumnX[column], y, color);
    }
  }
  for (std::uint8_t group = 0U; group < data.playbackRows.size(); ++group) {
    const std::array<std::int8_t, 2> playbackRows{
        data.playbackRows[group], data.automationPlaybackRows[group]};
    for (const std::int8_t playbackRow : playbackRows) {
      if (playbackRow >= 0 && playbackRow < 16)
        builder.Fill(
            PlaybackTickRect(group, static_cast<std::uint8_t>(playbackRow)),
            data.selectedTrackMuted ? UiColorToken::DerivedPlaybackMuted
                                    : UiColorToken::PlaybackActive);
    }
  }
  if (!data.numberFocus) {
    const RectI16 cursor = ResolvedCursorRect(data);
    bool cursorOverPlayback = false;
    for (std::uint8_t group = 0U; group < data.playbackRows.size(); ++group) {
      const std::array<std::int8_t, 2> playbackRows{
          data.playbackRows[group], data.automationPlaybackRows[group]};
      for (const std::int8_t playbackRow : playbackRows) {
        if (playbackRow >= 0 && playbackRow < 16 &&
            !Intersect(cursor,
                       PlaybackTickRect(
                           group, static_cast<std::uint8_t>(playbackRow)))
                 .Empty()) {
          cursorOverPlayback = true;
          break;
        }
      }
      if (cursorOverPlayback)
        break;
    }
    const UiSelectionStyle cursorStyle =
        !cursorOverPlayback
            ? UiSelectionStyle::Cursor
            : data.selectedTrackMuted ? UiSelectionStyle::MutedPlayback
                                      : UiSelectionStyle::Playback;
    builder.Selection(cursor, cursorStyle);
    if (data.cursorInkVisible && data.editRow < 16U &&
        data.editColumn < kColumnX.size()) {
      const std::string_view value =
          data.rows[data.editRow][data.editColumn];
      if (data.enterDigitFocus && IsParameterColumn(data.editColumn) &&
          !value.empty()) {
        const std::uint8_t digit = std::min<std::uint8_t>(
            data.editDigit, static_cast<std::uint8_t>(value.size() - 1U));
        builder.Text(value.substr(digit, 1),
                     static_cast<std::int16_t>(
                         kColumnX[data.editColumn] +
                         digit * UiFont5x7::kAdvance),
                     UiTrackerGridMetrics::RowTextY(data.editRow),
                     UiColorToken::TextHighlighted);
      } else {
        builder.Text(value, kColumnX[data.editColumn],
                     UiTrackerGridMetrics::RowTextY(data.editRow),
                     UiColorToken::TextHighlighted);
      }
    }
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
