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
  return UiFontView::CursorTargetRect(data.cursor);
}

void DrawSection(UiSceneBuilder<256, 1024> &builder, std::string_view label,
                 std::int16_t y) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.Text(label, 9, y, UiColorToken::TextColored);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(222 - width), 1},
               UiColorToken::CursorRow);
}

} // namespace

UiFontViewData UiFontViewState::ToViewData() const {
  UiFontViewData data;
  data.font = font.data();
  data.textCase = textCase.data();
  data.feedback = feedback.data();
  data.cursor = cursor;
  data.action = action;
  data.cursorVisualRect = cursorVisualRect;
  data.cursorVisualOverride = cursorVisualOverride;
  data.cursorInkVisible = cursorInkVisible;
  data.power = power;
  return data;
}

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
  if (previous.font != current.font || previous.textCase != current.textCase ||
      previous.feedback != current.feedback ||
      previous.cursor != current.cursor)
    render({0, 52, 240, 62});
  if (ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render({5, 52, 230, 18});
  }
  if (previous.action != current.action)
    render({0, 208, 240, 32});
}

UiBuildStatus UiFontView::Build(const UiFontViewData &data, UiPalette &,
                                UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{.title = "FONT", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  constexpr std::array<std::string_view, 2> actions{"BROWSE", "DEFAULT"};
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Selector};
  bottom.selector.options = actions;
  bottom.selector.current = data.action == UiFontAction::Browse ? 0U : 1U;
  bottom.selector.preserveCase = true;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  DrawSection(builder, "DISPLAY", 42);
  builder.Text("CASE", 9, 54, UiColorToken::TextDim);
  builder.LiteralText(data.textCase, 92, 54, UiColorToken::TextNormal);
  DrawSection(builder, "FACE", 74);
  builder.Text("FONT", 9, 86, UiColorToken::TextDim);
  builder.Text(data.font, 92, 86, UiColorToken::TextNormal);
  if (!data.feedback.empty())
    builder.Text(data.feedback, 9, 101, UiColorToken::TextColored);
  DrawSection(builder, "PREVIEW", 120);
  builder.Text("ABCDEFGH", 9, 135, UiColorToken::TextNormal, 2);
  builder.Text("01234567", 9, 153, UiColorToken::TextColored, 2);
  builder.Text("NOTE C#4  FX ARP", 9, 175, UiColorToken::TextDim);
  builder.Text("ROW 0A  VALUE FF", 9, 188, UiColorToken::TextNormal);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    if (data.cursor == UiFontCursor::TextCase) {
      builder.Text("CASE", 9, 54, UiColorToken::TextHighlighted);
      builder.LiteralText(data.textCase, 92, 54,
                          UiColorToken::TextHighlighted);
    } else {
      builder.Text("FONT", 9, 86, UiColorToken::TextHighlighted);
      builder.Text(data.font, 92, 86, UiColorToken::TextHighlighted);
    }
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
