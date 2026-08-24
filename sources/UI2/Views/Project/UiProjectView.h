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

enum class UiProjectCursor : std::uint8_t {
  Name,
  Tempo,
  Transpose,
  Scale,
  Root,
  SamplePool,
  Samples,
  Instruments,
  Render,
};

struct UiProjectViewData {
  std::string_view name = "ONECYCAC";
  std::string_view tempo = "163";
  std::string_view transpose = "00";
  std::string_view scale = "CHROMATIC";
  std::string_view root = "C";
  std::array<std::string_view, 5> selectorOptions{};
  std::uint8_t selectorCount = 0;
  std::uint8_t selectorCurrent = 0;
  bool selectorWrap = false;
  bool editHeld = false;
  UiProjectCursor cursor = UiProjectCursor::Tempo;
  std::uint8_t nameAction = 0;
  std::uint8_t sampleAction = 0;
  std::uint8_t renderOption = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  std::int16_t scrollOffset = 0;
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
  [[nodiscard]] static constexpr std::int16_t ContentBottom() { return 181; }
  [[nodiscard]] static std::int16_t
  RevealCursor(std::int16_t currentOffset, UiProjectCursor cursor) {
    return UiVerticalList::Reveal(currentOffset, CursorTargetRect(cursor), 34,
                                  208, ContentBottom());
  }
  [[nodiscard]] static RectI16 FieldDamageRect(std::int16_t y);
};

} // namespace ui2
