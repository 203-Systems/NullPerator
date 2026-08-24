/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Theme/UiThemeView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Theme/UiThemeSchema.h"

namespace ui2 {
namespace {

RectI16 ResolvedCursorRect(const UiThemeViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiThemeView::CursorTargetRect(data);
}

} // namespace

RectI16 UiThemeView::CursorTargetRect(const UiThemeViewData &data) {
  if (data.selectedColor < 0) return CursorTargetRect();
  return ColorCursorTargetRect(static_cast<std::uint8_t>(data.selectedColor));
}

std::int16_t UiThemeView::RevealCursor(std::int16_t currentOffset,
                                       const UiThemeViewData &data) {
  return UiVerticalList::Reveal(currentOffset, CursorTargetRect(data), 34,
                                kRevealBottom, kContentBottom);
}

void UiThemeView::RenderDelta(const UiThemeViewData &previous,
                              const UiThemeViewData &current,
                              const UiFrameScene &currentScene,
                              UiIndexedSurface &surface,
                              const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (previous.scrollOffset != current.scrollOffset) {
    render({0, 34, 240, 174});
    return;
  }
  if (previous.name != current.name)
    render(UiVerticalList::VisualRect({5, 40, 230, 11},
                                      current.scrollOffset));
  if (ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(UiVerticalList::VisualRect({5, 40, 230, kContentBottom - 40},
                                      current.scrollOffset));
  }
}

UiBuildStatus UiThemeView::Build(const UiThemeViewData &data, UiPalette &,
                                 UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.contentOffsetY = UiVerticalList::Clamp(data.scrollOffset,
                                                kRevealBottom,
                                                kContentBottom);
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{.title = "THEME", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = {"NEW", "LOAD", "SAVE", "RENAME"};
  bottom.actions.count = 4;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("NAME", 9, 42, UiColorToken::TextDim);
  builder.Text(data.name, 92, 42, UiColorToken::TextNormal);
  for (std::uint8_t index = 0; index < kUiThemeColors.size(); ++index) {
    const std::int16_t topY = static_cast<std::int16_t>(58 + index * 14);
    builder.Text(kUiThemeColors[index].label, 9,
                 static_cast<std::int16_t>(topY + 3),
                 UiColorToken::TextDim);
    builder.Fill({151, topY, 78, 10}, kUiThemeColors[index].token);
  }
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    if (data.selectedColor < 0) {
      builder.Text("NAME", 9, 42, UiColorToken::TextHighlighted);
      builder.Text(data.name, 92, 42, UiColorToken::TextHighlighted);
    } else if (static_cast<std::size_t>(data.selectedColor) <
               kUiThemeColors.size()) {
      const std::uint8_t index = static_cast<std::uint8_t>(data.selectedColor);
      const std::int16_t topY = static_cast<std::int16_t>(58 + index * 14);
      builder.Text(kUiThemeColors[index].label, 9,
                   static_cast<std::int16_t>(topY + 3),
                   UiColorToken::TextHighlighted);
      builder.Fill({151, topY, 78, 10}, kUiThemeColors[index].token);
    }
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
