/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Interaction/UiVerticalList.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiDeviceCursor : std::uint8_t {
  MidiDevice,
  MidiSync,
  LineOut,
  RemoteUi,
  Resampler,
  Volume,
  Brightness,
  Theme,
  Font,
  UpdateFirmware,
};

struct UiDeviceViewData {
  std::string_view midiDevice = "OFF";
  std::string_view midiSync = "OFF";
  std::string_view lineOut = "LINE LEVEL";
  std::string_view remoteUi = "ON";
  std::string_view resampler = "NONE";
  std::string_view volume = "40";
  std::string_view brightness = "FF";
  std::string_view theme = "DEFAULT";
  std::string_view font = "REGULAR";
  std::string_view version = "VERSION 2.3 BETA";
  std::array<std::string_view, 8> selectorOptions{};
  std::uint8_t selectorCount = 0;
  std::uint8_t selectorCurrent = 0;
  bool selectorWrap = false;
  bool showLineOut = false;
  bool showVolume = true;
  bool showTheme = true;
  bool showFont = true;
  bool showUpdateFirmware = false;
  std::uint8_t batteryPercent = 60;
  UiDeviceCursor cursor = UiDeviceCursor::MidiDevice;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  std::int16_t scrollOffset = 0;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiDeviceView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiDeviceViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiDeviceViewData &previous,
                          const UiDeviceViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16
  CursorTargetRect(const UiDeviceViewData &data);
  [[nodiscard]] static std::int16_t
  ContentBottom(const UiDeviceViewData &data);
  [[nodiscard]] static std::int16_t
  RevealCursor(std::int16_t currentOffset, const UiDeviceViewData &data);
  [[nodiscard]] static RectI16 FieldDamageRect(std::int16_t y);
};

} // namespace ui2
