/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Record/UiRecordView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>

namespace ui2 {
namespace {

RectI16 ResolvedCursorRect(const UiRecordViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiRecordView::CursorTargetRect();
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty()) return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
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

void UiRecordView::RenderDelta(const UiRecordViewData &previous,
                               const UiRecordViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power || previous.source != current.source)
    render({0, 0, 240, 34});
  if (previous.source != current.source) render({5, 40, 230, 12});
  if (previous.lineGain != current.lineGain) render({5, 52, 230, 11});
  if (previous.micGain != current.micGain) render({5, 63, 230, 11});
  if (previous.safeWidth != current.safeWidth ||
      previous.warningWidth != current.warningWidth) {
    render({9, 100, 222, 14});
  }
  if (previous.elapsed != current.elapsed) render({80, 128, 80, 22});

  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
}

UiBuildStatus UiRecordView::Build(const UiRecordViewData &data,
                                  UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{
      .title = "RECORD", .meta = data.source, .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built) return topStatus;
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Actions};
  bottom.actions.actions = {"MONITOR", "RECORD", {}, {}};
  bottom.actions.count = 2;
  bottom.actions.active = 1;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built) return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("SOURCE", 9, 43, UiColorToken::TextDim);
  builder.Text(data.source, 92, 43, UiColorToken::TextNormal);
  builder.Text("LINE GAIN", 9, 54, UiColorToken::TextDim);
  builder.Text(data.lineGain, 92, 54, UiColorToken::TextNormal);
  builder.Text("MIC GAIN", 9, 65, UiColorToken::TextDim);
  builder.Text(data.micGain, 92, 65, UiColorToken::TextNormal);
  DrawSection(builder, "LEVEL", 84);
  builder.Fill({9, 100, 222, 14}, UiColorToken::DerivedVuTrack);
  const std::int16_t safe = static_cast<std::int16_t>(
      std::min<std::uint16_t>(data.safeWidth, 222));
  builder.Fill({9, 100, safe, 14}, UiColorToken::VuSafe);
  const std::int16_t warning = static_cast<std::int16_t>(
      std::min<std::uint16_t>(data.warningWidth, 222 - safe));
  builder.Fill({static_cast<std::int16_t>(9 + safe), 100, warning, 14},
               UiColorToken::VuWarning);
  builder.CenteredText(data.elapsed, 120, 132, UiColorToken::TextNormal, 2);
  builder.CenteredText("PRESS PLAY TO RECORD", 120, 164,
                       UiColorToken::PlaybackActive);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    builder.Text("SOURCE", 9, 43, UiColorToken::TextHighlighted);
    builder.Text(data.source, 92, 43, UiColorToken::TextHighlighted);
  }
  return builder.Ok() ? UiBuildStatus::Built
                      : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
