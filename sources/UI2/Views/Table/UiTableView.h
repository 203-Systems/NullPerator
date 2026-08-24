/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiTableHeader : std::uint8_t { None, Fx1, Fx2, Fx3 };

struct UiTableViewData {
  std::string_view number = "P00";
  std::string_view elapsed = "00:08";
  std::array<std::array<std::string_view, 6>, 16> rows{};
  std::array<std::string_view, 8> trackNotes{};
  UiBottomBarModel cursorBottom{};
  std::uint8_t rowOffset = 0;
  std::uint8_t editRow = 0;
  std::uint8_t editColumn = 0;
  std::uint8_t editDigit = 3;
  std::int8_t selectedTrack = 0;
  UiTableHeader activeHeader = UiTableHeader::None;
  RectI16 cursorVisualRect{};
  RectI16 selectionVisualRect{};
  RectI16 topMetaVisualRect{};
  RectI16 bottomTrackVisualRect{};
  bool cursorVisualOverride = false;
  bool topMetaVisualOverride = false;
  bool bottomTrackVisualOverride = false;
  bool cursorInkVisible = true;
  bool topMetaInkVisible = true;
  bool bottomTrackInkVisible = true;
  bool enterDigitFocus = false;
  bool numberFocus = false;
  bool adjustmentFocus = false;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiTableView {
public:
  [[nodiscard]] static UiBuildStatus
  Build(const UiTableViewData &data, UiPalette &palette, UiFrameScene &scene);
  static void RenderDelta(const UiTableViewData &previous,
                          const UiTableViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16 CursorTargetRect(const UiTableViewData &data);
  [[nodiscard]] static RectI16 SelectionTargetRect(std::int16_t left,
                                                   std::int16_t top,
                                                   std::int16_t right,
                                                   std::int16_t bottom);
  [[nodiscard]] static RectI16 RowDamageRect(std::uint8_t row);

private:
  [[nodiscard]] static bool
  RequiresFullInvalidation(const UiTableViewData &previous,
                           const UiTableViewData &current);
};

} // namespace ui2
