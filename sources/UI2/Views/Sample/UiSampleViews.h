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

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ui2 {

enum class UiSampleWaveformMarkerKind : std::uint8_t {
  Start,
  End,
  Slice,
  Playhead,
};

struct UiSampleWaveformMarker {
  std::uint8_t x = 0;
  UiSampleWaveformMarkerKind kind = UiSampleWaveformMarkerKind::Slice;
  bool selected = false;

  bool operator==(const UiSampleWaveformMarker &) const = default;
};

enum class UiSampleEditorCursor : std::uint8_t {
  Name,
  Waveform,
  Start,
  End,
  Field3,
  Field4,
  Save,
  SaveAndLoad,
  Discard,
  None,
};

struct UiSampleEditorViewData {
  std::string_view name = "AKWF 0906";
  std::string_view start = "000000";
  std::string_view end = "000258";
  // The approved fixture uses LOOP/GAIN. The application adapter replaces
  // these two generic rows with the real editor controller's OP/APPLY fields.
  std::string_view field3Label = "LOOP";
  std::string_view field3Value = "FORWARD";
  std::string_view field4Label = "GAIN";
  std::string_view field4Value = "00 DB";
  std::string_view help{};
  // Fixed-capacity packet copied into the frame scene; see
  // UiCommandList::SparseCoverageMask for its allocation-free encoding.
  std::span<const std::uint8_t> waveformMask{};
  std::span<const UiSampleWaveformMarker> markers{};
  std::uint32_t waveformRevision = 0;
  std::array<std::string_view, 4> bottomActions{"SAVE", "TRIM", "DISCARD",
                                                {}};
  std::uint8_t bottomActionCount = 3;
  std::uint8_t bottomActive = 0;
  UiSampleEditorCursor cursor = UiSampleEditorCursor::Start;
  std::uint8_t focusDigit = 0;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool enterDigitFocus = false;
  bool waveformReady = false;
  bool previewActive = false;
  bool singleCycle = false;
  bool projectPool = false;
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
  [[nodiscard]] static RectI16 CursorTargetRect(UiSampleEditorCursor cursor);
  [[nodiscard]] static RectI16
  CursorTargetRect(const UiSampleEditorViewData &data) {
    RectI16 target = CursorTargetRect(data.cursor);
    if (data.enterDigitFocus &&
        (data.cursor == UiSampleEditorCursor::Start ||
         data.cursor == UiSampleEditorCursor::End)) {
      const std::uint8_t digit = std::min<std::uint8_t>(data.focusDigit, 6U);
      target.x = static_cast<std::int16_t>(90 + digit * 6);
      target.width = 9;
    }
    return target;
  }
};

enum class UiSampleSlicesCursor : std::uint8_t {
  Status,
  Waveform,
  AutoSliceCount,
  AutoSlice,
  None,
};

struct UiSampleSlicesViewData {
  std::string_view sliceCount = "04";
  std::string_view slice = "01 / 04";
  std::string_view start = "000064";
  std::string_view zoom = "1X";
  std::string_view autoSliceCount{};
  std::string_view help = "NAV SELECT  EDIT FINE";
  // The model only rebuilds this packet when waveformRevision changes.
  std::span<const std::uint8_t> waveformMask{};
  std::span<const UiSampleWaveformMarker> markers{};
  std::uint32_t waveformRevision = 0;
  std::uint8_t selectedMarker = 1;
  UiSampleSlicesCursor cursor = UiSampleSlicesCursor::Status;
  RectI16 cursorVisualRect{};
  bool cursorVisualOverride = false;
  bool cursorInkVisible = true;
  bool waveformReady = false;
  bool hasSample = false;
  bool previewActive = false;
  bool previewPlayheadVisible = false;
  bool autoSliceApplyAvailable = true;
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
  [[nodiscard]] static RectI16 CursorTargetRect(UiSampleSlicesCursor cursor);
  [[nodiscard]] static RectI16
  CursorTargetRect(const UiSampleSlicesViewData &data) {
    return CursorTargetRect(data.cursor);
  }
};

} // namespace ui2
