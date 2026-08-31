/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Theme/UiThemeView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Theme/UiThemeSchema.h"

#include <algorithm>

namespace ui2 {
namespace {

RectI16 ResolvedCursorRect(const UiThemeViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiThemeView::CursorTargetRect(data);
}

} // namespace

UiThemeViewData UiThemeViewState::ToViewData() const {
  UiThemeViewData data;
  data.name = name.data();
  data.selectedColor = selectedColor;
  data.selectedRgb = selectedRgb;
  data.colorComponent = colorComponent;
  data.nameAction = nameAction;
  data.scrollOffset = scrollOffset;
  data.cursorVisualRect = cursorVisualRect;
  data.cursorVisualOverride = cursorVisualOverride;
  data.cursorInkVisible = cursorInkVisible;
  data.power = power;
  return data;
}

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
  const bool contentRedrawn = previous.scrollOffset != current.scrollOffset;
  if (contentRedrawn) {
    render({0, 34, 240, 174});
  }
  if (!contentRedrawn && previous.name != current.name)
    render(UiVerticalList::VisualRect({5, 40, 230, 11},
                                      current.scrollOffset));
  if (!contentRedrawn &&
      (ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
       previous.cursorInkVisible != current.cursorInkVisible)) {
    render(UiVerticalList::VisualRect({5, 40, 230, kContentBottom - 40},
                                      current.scrollOffset));
  }
  if (previous.selectedColor != current.selectedColor ||
      previous.nameAction != current.nameAction ||
      previous.selectedRgb != current.selectedRgb ||
      previous.colorComponent != current.colorComponent)
    render({0, 208, 240, 32});
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
  UiBottomBarModel bottom{};
  if (data.selectedColor < 0) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"NEW", "LOAD", "SAVE", "RENAME"};
    bottom.actions.count = 4;
    bottom.actions.active = std::min<std::uint8_t>(data.nameAction, 3);
  } else {
    bottom.kind = UiBottomBarKind::Rgb;
    bottom.rgb.values = data.selectedRgb;
    bottom.rgb.active = std::min<std::uint8_t>(data.colorComponent, 2U);
  }
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("NAME", 9, 42, UiColorToken::TextDim);
  builder.UserText(data.name, 92, 42, UiColorToken::TextNormal);
  for (std::uint8_t index = 0; index < kUiThemeColors.size(); ++index) {
    const std::int16_t topY = static_cast<std::int16_t>(
        kColorFirstTop + index * kColorRowPitch);
    builder.Text(kUiThemeColors[index].label, 9,
                 static_cast<std::int16_t>(topY + 2),
                 UiColorToken::TextDim);
    builder.Fill({151, topY, 78, 10}, kUiThemeColors[index].token);
  }
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    if (data.selectedColor < 0) {
      builder.Text("NAME", 9, 42, UiColorToken::TextHighlighted);
      builder.UserText(data.name, 92, 42, UiColorToken::TextHighlighted);
    } else if (static_cast<std::size_t>(data.selectedColor) <
               kUiThemeColors.size()) {
      const std::uint8_t index = static_cast<std::uint8_t>(data.selectedColor);
      const std::int16_t topY = static_cast<std::int16_t>(
          kColorFirstTop + index * kColorRowPitch);
      builder.Text(kUiThemeColors[index].label, 9,
                   static_cast<std::int16_t>(topY + 2),
                   UiColorToken::TextHighlighted);
      builder.Fill({151, topY, 78, 10}, kUiThemeColors[index].token);
    }
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
