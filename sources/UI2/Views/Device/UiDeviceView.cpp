/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Device/UiDeviceView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::string_view, 2> kBooleanOptions{"OFF", "ON"};

struct DeviceLayout {
  std::int16_t connections = 42;
  std::int16_t midiDevice = 54;
  std::int16_t midiSync = 65;
  std::int16_t remoteUi = 76;
  std::int16_t audio = 93;
  std::int16_t resampler = 105;
  std::int16_t lineOut = -1;
  std::int16_t volume = -1;
  std::int16_t display = 122;
  std::int16_t brightness = 134;
  std::int16_t theme = -1;
  std::int16_t font = -1;
  std::int16_t version = 161;
  std::int16_t maintenance = -1;
  std::int16_t updateFirmware = -1;
  std::int16_t contentBottom = 170;
};

DeviceLayout LayoutFor(const UiDeviceViewData &data) {
  DeviceLayout layout;
  std::int16_t lastAudio = layout.resampler;
  if (data.showLineOut) {
    layout.lineOut = static_cast<std::int16_t>(lastAudio + 11);
    lastAudio = layout.lineOut;
  }
  if (data.showVolume) {
    layout.volume = static_cast<std::int16_t>(lastAudio + 11);
    lastAudio = layout.volume;
  }
  layout.display = static_cast<std::int16_t>(lastAudio + 17);
  layout.brightness = static_cast<std::int16_t>(layout.display + 12);
  std::int16_t lastDisplay = layout.brightness;
  if (data.showTheme) {
    layout.theme = static_cast<std::int16_t>(lastDisplay + 11);
    lastDisplay = layout.theme;
  }
  if (data.showFont) {
    layout.font = static_cast<std::int16_t>(lastDisplay + 11);
    lastDisplay = layout.font;
  }
  layout.version = static_cast<std::int16_t>(lastDisplay + 27);
  layout.contentBottom = static_cast<std::int16_t>(layout.version + 9);
  if (data.showUpdateFirmware) {
    layout.maintenance = static_cast<std::int16_t>(layout.version + 22);
    layout.updateFirmware =
        static_cast<std::int16_t>(layout.maintenance + 12);
    layout.contentBottom =
        static_cast<std::int16_t>(layout.updateFirmware + 9);
  }
  return layout;
}

RectI16 RowRect(std::int16_t y) {
  return y >= 0 ? RectI16{7, static_cast<std::int16_t>(y - 1), 226, 9}
                : RectI16{};
}

RectI16 ResolvedCursorRect(const UiDeviceViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiDeviceView::CursorTargetRect(data);
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
               std::string_view value, std::int16_t y,
               UiColorToken labelColor = UiColorToken::TextDim,
               UiColorToken valueColor = UiColorToken::TextNormal) {
  if (y < 0)
    return;
  builder.Text(label, 9, y, labelColor);
  builder.Text(value, 92, y, valueColor);
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
                     const UiDeviceViewData &data,
                     const DeviceLayout &layout) {
  switch (data.cursor) {
  case UiDeviceCursor::MidiDevice:
    DrawField(builder, "MIDI DEVICE", data.midiDevice, layout.midiDevice,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::MidiSync:
    DrawField(builder, "MIDI SYNC", data.midiSync, layout.midiSync,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::LineOut:
    DrawField(builder, "LINE OUT", data.lineOut, layout.lineOut,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::RemoteUi:
    DrawField(builder, "REMOTE UI", data.remoteUi, layout.remoteUi,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::Resampler:
    DrawField(builder, "RESAMPLER", data.resampler, layout.resampler,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::Volume:
    DrawField(builder, "VOLUME", data.volume, layout.volume,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::Brightness:
    DrawField(builder, "BRIGHTNESS", data.brightness, layout.brightness,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::Theme:
    DrawField(builder, "THEME", data.theme, layout.theme,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::Font:
    DrawField(builder, "FONT", data.font, layout.font,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  case UiDeviceCursor::UpdateFirmware:
    DrawField(builder, "UPDATE FIRMWARE", {}, layout.updateFirmware,
              UiColorToken::TextHighlighted,
              UiColorToken::TextHighlighted);
    break;
  }
}

bool StructureChanged(const UiDeviceViewData &previous,
                      const UiDeviceViewData &current) {
  return previous.showLineOut != current.showLineOut ||
         previous.showVolume != current.showVolume ||
         previous.showTheme != current.showTheme ||
         previous.showFont != current.showFont ||
         previous.showUpdateFirmware != current.showUpdateFirmware;
}

bool CursorUsesSelector(UiDeviceCursor cursor) {
  switch (cursor) {
  case UiDeviceCursor::MidiDevice:
  case UiDeviceCursor::MidiSync:
  case UiDeviceCursor::LineOut:
  case UiDeviceCursor::RemoteUi:
  case UiDeviceCursor::Resampler:
    return true;
  case UiDeviceCursor::Volume:
  case UiDeviceCursor::Brightness:
  case UiDeviceCursor::Theme:
  case UiDeviceCursor::Font:
  case UiDeviceCursor::UpdateFirmware:
    return false;
  }
  return false;
}

} // namespace

RectI16 UiDeviceView::CursorTargetRect(const UiDeviceViewData &data) {
  const DeviceLayout layout = LayoutFor(data);
  switch (data.cursor) {
  case UiDeviceCursor::MidiDevice:
    return RowRect(layout.midiDevice);
  case UiDeviceCursor::MidiSync:
    return RowRect(layout.midiSync);
  case UiDeviceCursor::LineOut:
    return RowRect(layout.lineOut);
  case UiDeviceCursor::RemoteUi:
    return RowRect(layout.remoteUi);
  case UiDeviceCursor::Resampler:
    return RowRect(layout.resampler);
  case UiDeviceCursor::Volume:
    return RowRect(layout.volume);
  case UiDeviceCursor::Brightness:
    return RowRect(layout.brightness);
  case UiDeviceCursor::Theme:
    return RowRect(layout.theme);
  case UiDeviceCursor::Font:
    return RowRect(layout.font);
  case UiDeviceCursor::UpdateFirmware:
    return RowRect(layout.updateFirmware);
  }
  return {};
}

std::int16_t UiDeviceView::ContentBottom(const UiDeviceViewData &data) {
  return LayoutFor(data).contentBottom;
}

std::int16_t UiDeviceView::RevealCursor(std::int16_t currentOffset,
                                        const UiDeviceViewData &data) {
  return UiVerticalList::Reveal(currentOffset, CursorTargetRect(data), 34, 208,
                                ContentBottom(data));
}

RectI16 UiDeviceView::FieldDamageRect(std::int16_t y) {
  return Intersect({5, static_cast<std::int16_t>(y - 1), 230, 11},
                   RectI16::Screen());
}

void UiDeviceView::RenderDelta(const UiDeviceViewData &previous,
                               const UiDeviceViewData &current,
                               const UiFrameScene &currentScene,
                               UiIndexedSurface &surface,
                               const UiPalette &palette) {
  if (StructureChanged(previous, current)) {
    UiFrameRenderer::RenderStatic(currentScene, surface, palette);
    return;
  }
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
  if (contentRedrawn)
    render({0, 34, 240, 174});
  const DeviceLayout layout = LayoutFor(current);
  const auto redrawField = [&](bool changed, std::int16_t y) {
    if (!contentRedrawn && changed && y >= 0)
      render(contentRect(FieldDamageRect(y)));
  };
  redrawField(previous.midiDevice != current.midiDevice, layout.midiDevice);
  redrawField(previous.midiSync != current.midiSync, layout.midiSync);
  redrawField(previous.lineOut != current.lineOut, layout.lineOut);
  redrawField(previous.remoteUi != current.remoteUi, layout.remoteUi);
  redrawField(previous.resampler != current.resampler, layout.resampler);
  redrawField(previous.volume != current.volume, layout.volume);
  redrawField(previous.brightness != current.brightness, layout.brightness);
  redrawField(previous.theme != current.theme, layout.theme);
  redrawField(previous.font != current.font, layout.font);
  redrawField(previous.version != current.version, layout.version);

  const RectI16 oldCursor = contentRect(ResolvedCursorRect(previous));
  const RectI16 newCursor = contentRect(ResolvedCursorRect(current));
  if (!contentRedrawn &&
      (oldCursor != newCursor || previous.cursor != current.cursor ||
       previous.cursorInkVisible != current.cursorInkVisible)) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
  }
  if (previous.cursor != current.cursor ||
      previous.selectorOptions != current.selectorOptions ||
      previous.selectorCount != current.selectorCount ||
      previous.selectorCurrent != current.selectorCurrent ||
      previous.selectorWrap != current.selectorWrap ||
      previous.midiDevice != current.midiDevice) {
    render({0, 208, 240, 32});
  }
}

UiBuildStatus UiDeviceView::Build(const UiDeviceViewData &data, UiPalette &,
                                  UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.contentOffsetY = UiVerticalList::Clamp(
      data.scrollOffset, 208, ContentBottom(data));
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;
  const UiTopBarModel top{.title = "DEVICE",
                          .power = data.power,
                          .showBatteryPercent = true,
                          .batteryPercent = data.batteryPercent};
  const UiBuildStatus topStatus = UiChromeRenderer::BuildTop(top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;

  UiBottomBarModel bottom{.kind = UiBottomBarKind::Hidden};
  if (CursorUsesSelector(data.cursor)) {
    bottom.kind = UiBottomBarKind::Selector;
    if (data.selectorCount > 0) {
      const std::uint8_t count = std::min<std::uint8_t>(
          data.selectorCount,
          static_cast<std::uint8_t>(data.selectorOptions.size()));
      bottom.selector.options =
          std::span<const std::string_view>(data.selectorOptions.data(), count);
      bottom.selector.current =
          std::min<std::uint8_t>(data.selectorCurrent, count - 1);
      bottom.selector.wrap = data.selectorWrap;
    } else {
      // Preserve the approved two-state fixture while live snapshots provide
      // each real Config option table (for example MIDI's four choices).
      bottom.selector.options = kBooleanOptions;
      bottom.selector.current = data.midiDevice == "ON" ? 1 : 0;
    }
  } else if (data.cursor == UiDeviceCursor::Theme ||
             data.cursor == UiDeviceCursor::Font) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"BROWSE", {}, {}, {}};
    bottom.actions.count = 1;
  } else if (data.cursor == UiDeviceCursor::UpdateFirmware) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"UPDATE", {}, {}, {}};
    bottom.actions.count = 1;
  }
  scene.bottomVisible = bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  const DeviceLayout layout = LayoutFor(data);
  UiSceneBuilder<256, 1024> builder(scene.content);
  DrawSection(builder, "CONNECTIONS", layout.connections);
  DrawField(builder, "MIDI DEVICE", data.midiDevice, layout.midiDevice);
  DrawField(builder, "MIDI SYNC", data.midiSync, layout.midiSync);
  DrawField(builder, "REMOTE UI", data.remoteUi, layout.remoteUi);
  DrawSection(builder, "AUDIO", layout.audio);
  DrawField(builder, "RESAMPLER", data.resampler, layout.resampler);
  DrawField(builder, "LINE OUT", data.lineOut, layout.lineOut);
  DrawField(builder, "VOLUME", data.volume, layout.volume);
  DrawSection(builder, "DISPLAY", layout.display);
  DrawField(builder, "BRIGHTNESS", data.brightness, layout.brightness);
  DrawField(builder, "THEME", data.theme, layout.theme);
  DrawField(builder, "FONT", data.font, layout.font);
  builder.Text(data.version, 9, layout.version, UiColorToken::DerivedTextFaint);
  if (layout.maintenance >= 0) {
    DrawSection(builder, "MAINTENANCE", layout.maintenance);
    DrawField(builder, "UPDATE FIRMWARE", {}, layout.updateFirmware);
  }
  const RectI16 cursor = ResolvedCursorRect(data);
  if (!cursor.Empty()) {
    builder.Selection(cursor);
    if (data.cursorInkVisible)
      DrawSelectedInk(builder, data, layout);
  }
  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
