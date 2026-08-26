/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Interaction/UiVerticalList.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Views/Instrument/UiInstrumentCapacity.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ui2 {

enum class UiInstrumentKind : std::uint8_t { None, Sample, Midi, Sid, Opal };
enum class UiInstrumentCursor : std::uint8_t {
  None,
  Name,
  Type,
  Field,
  Operator1,
  Operator2,
};

struct UiInstrumentField {
  std::string_view label;
  std::string_view value;
  std::int16_t y = 0;
  bool userData = false;
  bool operator==(const UiInstrumentField &) const = default;
};

struct UiInstrumentOperatorRow {
  std::string_view label;
  std::string_view op1;
  std::string_view op2;
  bool operator==(const UiInstrumentOperatorRow &) const = default;
};

struct UiInstrumentViewData {
  std::string_view number = "00";
  std::string_view elapsed = "00:08";
  std::string_view name = "--";
  UiInstrumentKind kind = UiInstrumentKind::None;
  std::array<UiInstrumentField, kUiInstrumentMaximumFields> fields{};
  std::uint8_t fieldCount = 0;
  std::array<UiInstrumentOperatorRow, 6> operators{};
  std::uint8_t operatorCount = 0;
  std::uint8_t selectedField = 0;
  std::uint8_t selectedOperator = 0;
  std::uint8_t nameAction = 0;
  std::array<std::string_view, 8> trackNotes{};
  std::int8_t selectedTrack = 0;
  UiInstrumentCursor cursor = UiInstrumentCursor::Type;
  RectI16 cursorVisualRect{};
  RectI16 topMetaVisualRect{};
  RectI16 bottomTrackVisualRect{};
  bool cursorVisualOverride = false;
  bool topMetaVisualOverride = false;
  bool bottomTrackVisualOverride = false;
  bool cursorInkVisible = true;
  bool topMetaInkVisible = true;
  bool bottomTrackInkVisible = true;
  bool numberFocus = false;
  bool enterSubfieldFocus = false;
  bool adjustmentFocus = false;
  bool adjustmentNote = false;
  std::uint8_t adjustmentFineStep = 1;
  std::uint8_t adjustmentCoarseStep = 10;
  std::uint8_t selectedSubfield = 0;
  std::uint8_t subfieldTextOffset = 0;
  std::int16_t scrollOffset = 0;
  UiNavCursorModel navCursor{};
  UiPowerState power = UiPowerState::BatteryNormal;
};

class UiInstrumentView {
public:
  [[nodiscard]] static UiBuildStatus Build(const UiInstrumentViewData &data,
                                           UiPalette &palette,
                                           UiFrameScene &scene);
  static void RenderDelta(const UiInstrumentViewData &previous,
                          const UiInstrumentViewData &current,
                          const UiFrameScene &currentScene,
                          UiIndexedSurface &surface, const UiPalette &palette);
  [[nodiscard]] static RectI16
  CursorTargetRect(const UiInstrumentViewData &data);
  [[nodiscard]] static std::int16_t
  ContentBottom(const UiInstrumentViewData &data);
  [[nodiscard]] static std::int16_t
  RevealCursor(std::int16_t currentOffset,
               const UiInstrumentViewData &data);
  [[nodiscard]] static RectI16 FieldDamageRect(std::int16_t y);

private:
  [[nodiscard]] static bool
  RequiresFullInvalidation(const UiInstrumentViewData &previous,
                           const UiInstrumentViewData &current);
};

} // namespace ui2
