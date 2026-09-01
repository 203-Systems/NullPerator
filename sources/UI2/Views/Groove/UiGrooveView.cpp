/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Groove/UiGrooveView.h"

#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Render/UiFrameRenderer.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

std::array<char, 3> HexByte(std::uint8_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  return {digits[value >> 4U], digits[value & 0x0FU], 0};
}

RectI16 ResolvedCursorRect(const UiGrooveViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiGrooveView::CursorTargetRect(data.editRow);
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

RectI16 UiGrooveView::CursorTargetRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {27, static_cast<std::int16_t>(48 + row * 9), 15, 9};
}

RectI16 UiGrooveView::SelectionTargetRect(std::int16_t top,
                                          std::int16_t bottom) {
  top = std::clamp<std::int16_t>(top, 0, 15);
  bottom = std::clamp<std::int16_t>(bottom, 0, 15);
  if (top > bottom)
    std::swap(top, bottom);
  return {27, static_cast<std::int16_t>(48 + top * 9), 15,
          static_cast<std::int16_t>((bottom - top + 1) * 9)};
}

RectI16 UiGrooveView::RowDamageRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {5, static_cast<std::int16_t>(47 + row * 9), 40, 11};
}

RectI16 UiGrooveView::PlaybackTickRect(std::uint8_t row) {
  if (row >= 16U)
    return {};
  return {26, static_cast<std::int16_t>(50 + row * 9), 2, 5};
}

void UiGrooveView::RenderDelta(const UiGrooveViewData &previous,
                               const UiGrooveViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.number != current.number)
    render({80, 0, 40, 34});
  if (previous.power != current.power ||
      previous.navCursor != current.navCursor)
    render({184, 0, 56, 34});

  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
    render(RowDamageRect(previous.editRow));
    render(RowDamageRect(current.editRow));
  }
  if (previous.playbackRow != current.playbackRow ||
      previous.selectedTrackMuted != current.selectedTrackMuted) {
    if (previous.playbackRow >= 0 && previous.playbackRow < 16)
      render(RowDamageRect(static_cast<std::uint8_t>(previous.playbackRow)));
    if (current.playbackRow >= 0 && current.playbackRow < 16)
      render(RowDamageRect(static_cast<std::uint8_t>(current.playbackRow)));
  }
  if (previous.selectionVisualRect != current.selectionVisualRect) {
    render(previous.selectionVisualRect);
    render(current.selectionVisualRect);
  }
  for (std::uint8_t row = 0; row < 16U; ++row) {
    if (previous.steps[row] != current.steps[row])
      render(RowDamageRect(row));
  }
  if (previous.selectionActive != current.selectionActive ||
      previous.selectionNextExpansionAll !=
          current.selectionNextExpansionAll ||
      previous.clipboardReady != current.clipboardReady ||
      previous.clipboardWidth != current.clipboardWidth ||
      previous.clipboardHeight != current.clipboardHeight)
    render({0, 208, 240, 32});
}

UiBuildStatus UiGrooveView::Build(const UiGrooveViewData &data, UiPalette &,
                                  UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel pageTop{
      .title = "GROOVE",
      .meta = data.number,
      .power = data.power,
      .navTarget = UiNavTarget::Groove,
      .navCursor = data.navCursor,
  };
  const UiBottomBarModel hidden{.kind = UiBottomBarKind::Hidden};
  const UiResolvedChrome chrome = UiBarResolver::Resolve({
      .pageTop = pageTop,
      .pageDefault = hidden,
      .selectionActive = data.selectionActive,
      .selectionNextExpansionAll = data.selectionNextExpansionAll,
      .clipboardReady = data.clipboardReady,
      .clipboardWidth = data.clipboardWidth,
      .clipboardHeight = data.clipboardHeight,
  });
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
  builder.Text("STEP", 28, 39, UiColorToken::TextColored);
  const RectI16 cursor = ResolvedCursorRect(data);
  if (!data.selectionVisualRect.Empty())
    builder.SelectionHighlight(data.selectionVisualRect);
  for (std::uint8_t row = 0; row < 16U; ++row) {
    const std::int16_t y = static_cast<std::int16_t>(49 + row * 9);
    const auto rowText = HexByte(row);
    builder.Text(rowText.data(), 8, y,
                 row == data.editRow ? UiColorToken::TextColored
                                     : UiColorToken::DerivedTextFaint);
    const auto value = HexByte(data.steps[row]);
    const char *display = data.steps[row] == 0xFFU ? "--" : value.data();
    builder.Text(display, 29, y,
                 data.steps[row] == 0xFFU ? UiColorToken::DerivedTextFaint
                                          : UiColorToken::TextNormal);
  }
  if (data.playbackRow >= 0 && data.playbackRow < 16)
    builder.Fill(PlaybackTickRect(static_cast<std::uint8_t>(data.playbackRow)),
                 data.selectedTrackMuted ? UiColorToken::DerivedPlaybackMuted
                                         : UiColorToken::PlaybackActive);
  const bool cursorOverPlayback =
      data.playbackRow >= 0 && data.playbackRow < 16 &&
      !Intersect(cursor,
                 PlaybackTickRect(static_cast<std::uint8_t>(data.playbackRow)))
           .Empty();
  const UiSelectionStyle cursorStyle =
      !cursorOverPlayback
          ? UiSelectionStyle::Cursor
          : data.selectedTrackMuted ? UiSelectionStyle::MutedPlayback
                                    : UiSelectionStyle::Playback;
  builder.Selection(cursor, cursorStyle);
  if (data.cursorInkVisible && data.editRow < 16U) {
    const auto value = HexByte(data.steps[data.editRow]);
    const char *display =
        data.steps[data.editRow] == 0xFFU ? "--" : value.data();
    builder.Text(display, 29, static_cast<std::int16_t>(49 + data.editRow * 9),
                 UiColorToken::TextHighlighted);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
