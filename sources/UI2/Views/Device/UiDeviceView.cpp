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
  if (previous.power != current.power ||
      previous.batteryPercent != current.batteryPercent) {
    render({174, 0, 66, 34});
  }
  if (previous.midiDevice != current.midiDevice) {
    render(FieldDamageRect(54));
    render({0, 208, 240, 32});
  }
  if (previous.midiSync != current.midiSync)
    render(FieldDamageRect(65));
  if (previous.remoteUi != current.remoteUi)
    render(FieldDamageRect(76));
  if (previous.resampler != current.resampler)
    render(FieldDamageRect(105));
  if (previous.volume != current.volume)
    render(FieldDamageRect(116));
  if (previous.brightness != current.brightness)
    render(FieldDamageRect(145));
  if (previous.theme != current.theme)
    render(FieldDamageRect(156));
  if (previous.font != current.font)
    render(FieldDamageRect(167));
  if (previous.version != current.version)
    render(FieldDamageRect(194));

  const RectI16 oldCursor = ResolvedCursorRect(previous);
  const RectI16 newCursor = ResolvedCursorRect(current);
  if (oldCursor != newCursor ||
      previous.cursorInkVisible != current.cursorInkVisible) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
}

UiBuildStatus UiDeviceView::Build(const UiDeviceViewData &data, UiPalette &,
                                  UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.bottomVisible = true;
  scene.topBackground = UiColorToken::SurfaceBarDeep;
  scene.bottomBackground = UiColorToken::SurfaceBarDeep;
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
  builder.Text(data.version, 9, 194, UiColorToken::TextDim);
  builder.Selection(ResolvedCursorRect(data));
  if (data.cursorInkVisible) {
    builder.Text("MIDI DEVICE", 9, 54, UiColorToken::CursorInk);
    builder.Text(data.midiDevice, 92, 54, UiColorToken::CursorInk);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
