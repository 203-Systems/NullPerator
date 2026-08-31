/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Project/UiProjectView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>
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
  builder.Text(label, 9, y, UiColorToken::TextDim);
  builder.Text(value, 92, y, UiColorToken::TextNormal);
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

void DrawSelectedInk(UiSceneBuilder<256, 1024> &builder,
                     const UiProjectViewData &data) {
  switch (data.cursor) {
  case UiProjectCursor::Name:
    builder.Text("NAME", 9, 42, UiColorToken::TextHighlighted);
    builder.UserText(data.name, 92, 42, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::Tempo:
    builder.Text("TEMPO", 9, 70, UiColorToken::TextHighlighted);
    builder.Text(data.tempo, 92, 70, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::Transpose:
    builder.Text("TRANSPOSE", 9, 81, UiColorToken::TextHighlighted);
    builder.Text(data.transpose, 92, 81, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::Scale:
    builder.Text("SCALE", 9, 92, UiColorToken::TextHighlighted);
    builder.Text(data.scale, 92, 92, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::Root:
    builder.Text("ROOT", 9, 103, UiColorToken::TextHighlighted);
    builder.Text(data.root, 92, 103, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::SamplePool:
  case UiProjectCursor::Samples:
    builder.Text("SAMPLES", 9, 132, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::Instruments:
    builder.Text("INSTRUMENTS", 9, 143, UiColorToken::TextHighlighted);
    break;
  case UiProjectCursor::Render:
    builder.Text("RENDER", 9, 172, UiColorToken::TextHighlighted);
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
  case UiProjectCursor::Transpose:
    return {7, 80, 226, 9};
  case UiProjectCursor::Scale:
    return {7, 91, 226, 9};
  case UiProjectCursor::Root:
    return {7, 102, 226, 9};
  case UiProjectCursor::SamplePool:
  case UiProjectCursor::Samples:
    return {7, 131, 226, 9};
  case UiProjectCursor::Instruments:
    return {7, 142, 226, 9};
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
  const auto contentRect = [&](RectI16 rect) {
    return UiVerticalList::VisualRect(rect, currentScene.contentOffsetY);
  };
  if (previous.power != current.power ||
      previous.navCursor != current.navCursor)
    render({184, 0, 56, 34});
  const bool contentRedrawn = previous.scrollOffset != current.scrollOffset;
  if (contentRedrawn) render({0, 34, 240, 174});
  if (!contentRedrawn && previous.name != current.name)
    render(contentRect(FieldDamageRect(42)));
  if (!contentRedrawn && previous.tempo != current.tempo)
    render(contentRect(FieldDamageRect(70)));
  if (!contentRedrawn && previous.transpose != current.transpose)
    render(contentRect(FieldDamageRect(81)));
  if (!contentRedrawn && previous.scale != current.scale)
    render(contentRect(FieldDamageRect(92)));
  if (!contentRedrawn && previous.root != current.root)
    render(contentRect(FieldDamageRect(103)));

  const RectI16 oldCursor = contentRect(ResolvedCursorRect(previous));
  const RectI16 newCursor = contentRect(ResolvedCursorRect(current));
  if (!contentRedrawn && (oldCursor != newCursor ||
                          previous.cursor != current.cursor ||
                          previous.cursorInkVisible !=
                              current.cursorInkVisible)) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
  if (previous.cursor != current.cursor || previous.tempo != current.tempo ||
      previous.transpose != current.transpose ||
      previous.scale != current.scale || previous.root != current.root ||
      previous.selectorOptions != current.selectorOptions ||
      previous.selectorCount != current.selectorCount ||
      previous.selectorCurrent != current.selectorCurrent ||
      previous.selectorWrap != current.selectorWrap ||
      previous.enterHeld != current.enterHeld ||
      previous.nameAction != current.nameAction ||
      previous.sampleAction != current.sampleAction ||
      previous.renderOption != current.renderOption)
    render({0, 208, 240, 32});
}

UiBuildStatus UiProjectView::Build(const UiProjectViewData &data, UiPalette &,
                                   UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.contentOffsetY =
      UiVerticalList::Clamp(data.scrollOffset, 208, ContentBottom());
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel top{.title = "PROJECT",
                          .power = data.power,
                          .navTarget = UiNavTarget::Project,
                          .navCursor = data.navCursor};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Hidden};
  std::array<std::string_view, 1> valueOption{};
  if (data.cursor == UiProjectCursor::Name) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"NEW", "LOAD", "SAVE", "RENAME"};
    bottom.actions.count = 4;
    bottom.actions.active = std::min<std::uint8_t>(data.nameAction, 3);
  } else if (data.enterHeld &&
             (data.cursor == UiProjectCursor::Tempo ||
              data.cursor == UiProjectCursor::Transpose)) {
    bottom.kind = UiBottomBarKind::AdjustmentLegend;
    bottom.adjustment.fineStep = 1U;
    bottom.adjustment.coarseStep =
        data.cursor == UiProjectCursor::Tempo ? 10U : 12U;
  } else if (data.cursor == UiProjectCursor::Tempo ||
             data.cursor == UiProjectCursor::Transpose ||
             data.cursor == UiProjectCursor::Scale ||
             data.cursor == UiProjectCursor::Root) {
    bottom.kind = UiBottomBarKind::Selector;
    if (data.selectorCount > 0) {
      const std::uint8_t count = std::min<std::uint8_t>(
          data.selectorCount,
          static_cast<std::uint8_t>(data.selectorOptions.size()));
      bottom.selector.options =
          std::span<const std::string_view>(data.selectorOptions.data(), count);
      bottom.selector.current =
          std::min<std::uint8_t>(data.selectorCurrent, count - 1U);
      bottom.selector.wrap = data.selectorWrap;
    } else {
      switch (data.cursor) {
      case UiProjectCursor::Tempo:
        valueOption[0] = data.tempo;
        break;
      case UiProjectCursor::Transpose:
        valueOption[0] = data.transpose;
        break;
      case UiProjectCursor::Scale:
        valueOption[0] = data.scale;
        break;
      case UiProjectCursor::Root:
        valueOption[0] = data.root;
        break;
      default:
        break;
      }
      bottom.selector.options = valueOption;
      bottom.selector.current = 0;
    }
  } else if (data.cursor == UiProjectCursor::SamplePool ||
             data.cursor == UiProjectCursor::Samples) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"BROWSE", "REMOVE UNUSED", {}, {}};
    bottom.actions.count = 2;
    bottom.actions.active = std::min<std::uint8_t>(data.sampleAction, 1);
  } else if (data.cursor == UiProjectCursor::Instruments) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"REMOVE UNUSED", {}, {}, {}};
    bottom.actions.count = 1;
  } else if (data.cursor == UiProjectCursor::Render) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kRenderOptions;
    bottom.selector.current = std::min<std::uint8_t>(data.renderOption, 1);
  }
  scene.bottomVisible = bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  builder.Text("NAME", 9, 42, UiColorToken::TextDim);
  builder.UserText(data.name, 92, 42, UiColorToken::TextNormal);
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
