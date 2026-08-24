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
#include <span>
#include <string_view>

namespace ui2 {

struct UiSampleEditorViewData {
  std::string_view name = "AKWF 0906";
  std::string_view start = "000000";
  std::string_view end = "000258";
  std::string_view loop = "FORWARD";
  std::string_view gain = "00 DB";
  // Fixed-capacity packet copied into the frame scene; see
  // UiCommandList::SparseCoverageMask for its allocation-free encoding.
  std::span<const std::uint8_t> waveformMask{};
  std::uint32_t waveformRevision = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiSampleEditorView {
public:
  [[nodiscard]] static UiBuildStatus Build(const UiSampleEditorViewData &data,
                                           UiPalette &palette,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiSampleEditorViewData &previous,
                          const UiSampleEditorViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface,
                          const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 144, 226, 9};
  }
};

struct UiSampleSlicesViewData {
  std::string_view sliceCount = "04";
  std::string_view slice = "01 / 04";
  std::string_view start = "000064";
  std::string_view zoom = "1X";
  // The model only rebuilds this packet when waveformRevision changes.
  std::span<const std::uint8_t> waveformMask{};
  std::uint32_t waveformRevision = 0;
  std::uint8_t selectedMarker = 1;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiSampleSlicesView {
public:
  [[nodiscard]] static UiBuildStatus Build(const UiSampleSlicesViewData &data,
                                           UiPalette &palette,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiSampleSlicesViewData &previous,
                          const UiSampleSlicesViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface,
                          const UiPalette &palette);
  [[nodiscard]] static constexpr RectI16 CursorTargetRect() {
    return {7, 138, 226, 9};
  }
};

} // namespace ui2
