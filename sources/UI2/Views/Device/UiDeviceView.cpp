/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Device/UiDeviceView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::string_view, 2> kBooleanOptions{"OFF", "ON"};

RectI16 ResolvedCursorRect(const UiDeviceViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiDeviceView::CursorTargetRect();
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

} // namespace

RectI16 UiDeviceView::FieldDamageRect(std::int16_t y) {
  return Intersect({5, static_cast<std::int16_t>(y - 1), 230, 11},
                   RectI16::Screen());
}

void UiDeviceView::RenderDelta(const UiDeviceViewData &previous,
                               const UiDeviceViewData &current,
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
      previous.batteryPercent != current.batteryPercent) {
    render({174, 0, 66, 34});
  }
  const bool contentRedrawn = previous.scrollOffset != current.scrollOffset;
  if (contentRedrawn) render({0, 34, 240, 174});
  if (!contentRedrawn && previous.midiDevice != current.midiDevice) {
    render(contentRect(FieldDamageRect(54)));
    render({0, 208, 240, 32});
  }
  if (!contentRedrawn && previous.midiSync != current.midiSync)
    render(contentRect(FieldDamageRect(65)));
  if (!contentRedrawn && previous.remoteUi != current.remoteUi)
    render(contentRect(FieldDamageRect(76)));
  if (!contentRedrawn && previous.resampler != current.resampler)
    render(contentRect(FieldDamageRect(105)));
  if (!contentRedrawn && previous.volume != current.volume)
    render(contentRect(FieldDamageRect(116)));
  if (!contentRedrawn && previous.brightness != current.brightness)
    render(contentRect(FieldDamageRect(145)));
  if (!contentRedrawn && previous.theme != current.theme)
    render(contentRect(FieldDamageRect(156)));
  if (!contentRedrawn && previous.font != current.font)
    render(contentRect(FieldDamageRect(167)));
  if (!contentRedrawn && previous.version != current.version)
    render(contentRect(FieldDamageRect(194)));

  const RectI16 oldCursor = contentRect(ResolvedCursorRect(previous));
  const RectI16 newCursor = contentRect(ResolvedCursorRect(current));
  if (!contentRedrawn && (oldCursor != newCursor ||
                          previous.cursorInkVisible !=
                              current.cursorInkVisible)) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
}

UiBuildStatus UiDeviceView::Build(const UiDeviceViewData &data, UiPalette &,
                                  UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.contentOffsetY = UiVerticalList::Clamp(data.scrollOffset, 208, 203);
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{.title = "DEVICE",
                          .power = data.power,
                          .showBatteryPercent = true,
                          .batteryPercent = data.batteryPercent};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Selector};
  bottom.selector.options = kBooleanOptions;
  bottom.selector.current = data.midiDevice == "ON" ? 1 : 0;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  DrawSection(builder, "CONNECTIONS", 42);
  DrawField(builder, "MIDI DEVICE", data.midiDevice, 54);
  DrawField(builder, "MIDI SYNC", data.midiSync, 65);
  DrawField(builder, "REMOTE UI", data.remoteUi, 76);
  DrawSection(builder, "AUDIO", 93);
  DrawField(builder, "RESAMPLER", data.resampler, 105);
  DrawField(builder, "VOLUME", data.volume, 116);
  DrawSection(builder, "DISPLAY", 133);
  DrawField(builder, "BRIGHTNESS", data.brightness, 145);
  DrawField(builder, "THEME", data.theme, 156);
  DrawField(builder, "FONT", data.font, 167);
  builder.Text(data.version, 9, 194, UiColorToken::DerivedTextFaint);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    builder.Text("MIDI DEVICE", 9, 54, UiColorToken::TextHighlighted);
    builder.Text(data.midiDevice, 92, 54, UiColorToken::TextHighlighted);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
