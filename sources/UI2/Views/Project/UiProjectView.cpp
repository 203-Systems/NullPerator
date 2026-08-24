/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Project/UiProjectView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::string_view, 2> kRenderOptions{"MIXDOWN", "STEMS"};

RectI16 ResolvedCursorRect(const UiProjectViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiProjectView::CursorTargetRect(data.cursor);
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

void DrawField(UiSceneBuilder<256, 1024> &builder, std::string_view label,
               std::string_view value, std::int16_t y) {
  builder.Text(label, 9, y, UiColorToken::TextMuted);
  builder.Text(value, 92, y, UiColorToken::TextPrimary);
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

void DrawSelectedInk(UiSceneBuilder<256, 1024> &builder,
                     const UiProjectViewData &data) {
  switch (data.cursor) {
  case UiProjectCursor::Name:
    builder.Text("NAME", 9, 42, UiColorToken::CursorInk);
    builder.Text(data.name, 92, 42, UiColorToken::CursorInk);
    break;
  case UiProjectCursor::Tempo:
    builder.Text("TEMPO", 9, 70, UiColorToken::CursorInk);
    builder.Text(data.tempo, 92, 70, UiColorToken::CursorInk);
    break;
  case UiProjectCursor::Samples:
    builder.Text("SAMPLES", 9, 132, UiColorToken::CursorInk);
    break;
  case UiProjectCursor::Render:
    builder.Text("RENDER", 9, 172, UiColorToken::CursorInk);
    break;
  }
}

} // namespace

RectI16 UiProjectView::CursorTargetRect(UiProjectCursor cursor) {
  switch (cursor) {
  case UiProjectCursor::Name:
    return {7, 41, 226, 9};
  case UiProjectCursor::Tempo:
    return {7, 69, 226, 9};
  case UiProjectCursor::Samples:
    return {7, 131, 226, 9};
  case UiProjectCursor::Render:
    return {7, 171, 226, 9};
  }
  return {};
}

RectI16 UiProjectView::FieldDamageRect(std::int16_t y) {
  return Intersect({5, static_cast<std::int16_t>(y - 1), 230, 11},
                   RectI16::Screen());
}

void UiProjectView::RenderDelta(const UiProjectViewData &previous,
                                const UiProjectViewData &current,
                                const UiFrameScene &currentScene,
                                UiIndexedSurface &surface,
                                const UiPalette &palette) {
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  if (previous.power != current.power)
    render({184, 0, 56, 34});
  if (previous.name != current.name)
    render(FieldDamageRect(42));
  if (previous.tempo != current.tempo)
    render(FieldDamageRect(70));
  if (previous.transpose != current.transpose)
    render(FieldDamageRect(81));
  if (previous.scale != current.scale)
    render(FieldDamageRect(92));
  if (previous.root != current.root)
    render(FieldDamageRect(103));

  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
    render({0, 208, 240, 32});
  }
}

UiBuildStatus UiProjectView::Build(const UiProjectViewData &data, UiPalette &,
                                   UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  scene.bottomBackground = UiColorToken::SurfaceBarDeep;

  const UiTopBarModel top{.title = "PROJECT", .power = data.power};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Hidden};
  if (data.cursor == UiProjectCursor::Name) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"NEW", "LOAD", "SAVE", "RENAME"};
    bottom.actions.count = 4;
  } else if (data.cursor == UiProjectCursor::Samples) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"REMOVE UNUSED", {}, {}, {}};
    bottom.actions.count = 1;
  } else if (data.cursor == UiProjectCursor::Render) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kRenderOptions;
    bottom.selector.current = 0;
  }
  scene.bottomVisible = bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  DrawField(builder, "NAME", data.name, 42);
  DrawSection(builder, "PLAYBACK", 58);
  DrawField(builder, "TEMPO", data.tempo, 70);
  DrawField(builder, "TRANSPOSE", data.transpose, 81);
  DrawField(builder, "SCALE", data.scale, 92);
  DrawField(builder, "ROOT", data.root, 103);
  DrawSection(builder, "CLEANUP", 120);
  DrawField(builder, "SAMPLES", {}, 132);
  DrawField(builder, "INSTRUMENTS", {}, 143);
  DrawSection(builder, "EXPORT", 160);
  DrawField(builder, "RENDER", {}, 172);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible)
    DrawSelectedInk(builder, data);
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
