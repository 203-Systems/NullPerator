/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiProjectCursor : std::uint8_t { Name, Tempo, Samples, Render };

struct UiProjectViewData {
  std::string_view name = "ONECYCAC";
  std::string_view tempo = "163 / A3";
  std::string_view transpose = "00";
  std::string_view scale = "CHROMATIC";
  std::string_view root = "C";
  UiProjectCursor cursor = UiProjectCursor::Tempo;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiProjectView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiProjectViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiProjectViewData &previous,
                          const UiProjectViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16 CursorTargetRect(UiProjectCursor cursor);
  [[nodiscard]] static RectI16 FieldDamageRect(std::int16_t y);
};

} // namespace ui2
