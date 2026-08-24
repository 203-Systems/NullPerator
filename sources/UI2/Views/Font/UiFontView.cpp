/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Font/UiFontView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

namespace ui2 {
namespace {

RectI16 ResolvedCursorRect(const UiFontViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiFontView::CursorTargetRect();
}

void DrawSection(UiSceneBuilder<256, 1024> &builder, std::string_view label,
                 std::int16_t y) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.Text(label, 9, y, UiColorToken::CursorPrimary);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(222 - width), 1},
               UiColorToken::CursorRow);
}

} // namespace

void UiFontView::RenderDelta(const UiFontViewData &previous,
                             const UiFontViewData &current,
                             const UiFrameScene &currentScene,
                             UiIndexedSurface &surface,
                             const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (previous.font != current.font)
    render({5, 52, 230, 11});
  if (ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render({5, 52, 230, 18});
  }
}

UiBuildStatus UiFontView::Build(const UiFontViewData &data, UiPalette &,
                                UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomVisible = false;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  const UiTopBarModel top{.title = "FONT", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  DrawSection(builder, "FACE", 42);
  builder.Text("FONT", 9, 54, UiColorToken::TextMuted);
  builder.Text(data.font, 92, 54, UiColorToken::TextPrimary);
  builder.Text("BROWSE", 92, 68, UiColorToken::CursorPrimary);
  DrawSection(builder, "PREVIEW", 90);
  builder.Text("ABCDEFGH", 9, 105, UiColorToken::TextPrimary, 2);
  builder.Text("01234567", 9, 123, UiColorToken::CursorPrimary, 2);
  builder.Text("NOTE C#4  FX ARP", 9, 145, UiColorToken::TextMuted);
  builder.Text("ROW 0A  VALUE FF", 9, 158, UiColorToken::TextPrimary);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    builder.Text("FONT", 9, 54, UiColorToken::CursorInk);
    builder.Text(data.font, 92, 54, UiColorToken::CursorInk);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
