/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Browser/UiBrowserView.h"

#include "UI2/Render/UiFrameRenderer.h"

namespace ui2 {
namespace {

RectI16 ResolvedCursorRect(const UiBrowserViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiBrowserView::CursorTargetRect();
}

} // namespace

void UiBrowserView::RenderDelta(const UiBrowserViewData &previous,
                                const UiBrowserViewData &current,
                                const UiFrameScene &currentScene,
                                UiIndexedSurface &surface,
                                const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.title != current.title || previous.meta != current.meta) {
    render({0, 0, 184, 34});
  }
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (previous.item != current.item ||
      ResolvedCursorRect(previous) != ResolvedCursorRect(current) ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render({5, 40, 230, 20});
  }
  if (previous.footer != current.footer)
    render({5, 190, 230, 12});
  if (previous.actions != current.actions ||
      previous.actionCount != current.actionCount ||
      previous.activeAction != current.activeAction) {
    render({0, 208, 240, 32});
  }
}

UiBuildStatus UiBrowserView::Build(const UiBrowserViewData &data, UiPalette &,
                                   UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{
      .title = data.title, .meta = data.meta, .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = {
      data.actions[0], data.actions[1], data.actions[2], {}};
  bottom.actions.count = data.actionCount;
  bottom.actions.active = data.activeAction;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text(">", 10, 45, UiColorToken::TextNormal);
  builder.Text(data.item, 21, 45, UiColorToken::TextNormal);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    builder.Text(">", 10, 45, UiColorToken::TextHighlighted);
    builder.Text(data.item, 21, 45, UiColorToken::TextHighlighted);
  }
  builder.Text(data.footer, 9, 192, UiColorToken::DerivedTextFaint);
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
