/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Theme/UiThemeView.h"

#include "UI2/Render/UiFrameRenderer.h"

#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::string_view, 9> kLabels{
    "FOREGROUND", "BACKGROUND", "HIGHLIGHT 1", "HIGHLIGHT 2", "CURSOR",
    "INFO",       "WARNING",    "ERROR",       "ACCENT"};
constexpr std::array<UiColorToken, 9> kSwatches{
    UiColorToken::TextPrimary,   UiColorToken::SurfaceField,
    UiColorToken::CursorPrimary, UiColorToken::PlaybackActive,
    UiColorToken::CursorPrimary, UiColorToken::VuSafe,
    UiColorToken::VuWarning,     UiColorToken::VuPeak,
    UiColorToken::TextMuted};

RectI16 ResolvedCursorRect(const UiThemeViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiThemeView::CursorTargetRect();
}

} // namespace

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
  if (previous.name != current.name)
    render({5, 40, 230, 11});
  if (ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render({5, 40, 230, 18});
  }
}

UiBuildStatus UiThemeView::Build(const UiThemeViewData &data, UiPalette &,
                                 UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  scene.bottomBackground = UiColorToken::SurfaceBarDeep;
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
  builder.Text("NAME", 9, 42, UiColorToken::TextMuted);
  builder.Text(data.name, 92, 42, UiColorToken::TextPrimary);
  for (std::uint8_t index = 0; index < kLabels.size(); ++index) {
    const std::int16_t topY = static_cast<std::int16_t>(58 + index * 14);
    builder.Text(kLabels[index], 9, static_cast<std::int16_t>(topY + 3),
                 index == 2U ? UiColorToken::CursorPrimary
                             : UiColorToken::TextMuted);
    builder.Fill({151, topY, 78, 10}, kSwatches[index]);
  }
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    builder.Text("NAME", 9, 42, UiColorToken::CursorInk);
    builder.Text(data.name, 92, 42, UiColorToken::CursorInk);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
