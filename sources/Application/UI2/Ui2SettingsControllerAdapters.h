/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2SettingsSnapshots.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

// Bottom-action indices are part of the approved Theme renderer contract.
// Keeping them named here avoids coupling the legacy controller to the chrome
// implementation's array positions.
enum class UiThemeNameAction : std::uint8_t {
  New = 0,
  Load = 1,
  Save = 2,
  Rename = 3,
};

[[nodiscard]] constexpr std::uint8_t
ThemeNameActionBit(UiThemeNameAction action) {
  return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(action));
}

struct UiThemeControllerFocus {
  ThemeViewUi2Focus focus = ThemeViewUi2Focus::Unknown;
  std::int8_t selectedColor = -1;
  std::uint8_t nameAction = 0;
};

static_assert(std::is_trivially_copyable_v<UiThemeControllerFocus>);
static_assert(sizeof(UiThemeControllerFocus) <= 4U);

// ThemeView's real field list is:
//   NAME, IMPORT, EXPORT, FONT,
//   then twelve groups of LABEL, R, G, B, SWATCH.
// Static labels/swatches cannot normally receive focus, but resolving them to
// Unknown keeps a stale or malformed focus index from selecting another row.
inline constexpr std::array<std::int8_t, 12> kLegacyThemeColorToUi2{
    3,  // Foreground -> text.normal
    0,  // Background -> surface.bg
    -1, // Highlight1 has no one-to-one UI2 semantic field
    6,  // Highlight2 -> text.colored
    1,  // Console -> surface.top_bar
    7,  // Cursor -> cursor.primary
    10, // Info -> system.info
    11, // Warning -> system.warning
    12, // Error -> system.error
    9,  // Accent -> playback.active
    8,  // AccentAlt -> cursor.row
    4,  // Emphasis -> text.dim
};

inline constexpr std::uint8_t kLegacyThemeNameActionMask =
    ThemeNameActionBit(UiThemeNameAction::Load) |
    ThemeNameActionBit(UiThemeNameAction::Save) |
    ThemeNameActionBit(UiThemeNameAction::Rename);

[[nodiscard]] constexpr std::uint32_t LegacyThemeEditableColorMask() {
  std::uint32_t mask = 0;
  for (const std::int8_t color : kLegacyThemeColorToUi2) {
    if (color >= 0 &&
        static_cast<std::size_t>(color) < ThemeViewUi2Snapshot::ColorCount) {
      mask |= std::uint32_t{1}
              << static_cast<std::uint8_t>(color);
    }
  }
  return mask;
}

inline constexpr std::uint32_t kLegacyThemeEditableColorMask =
    LegacyThemeEditableColorMask();

inline constexpr std::uint8_t kApprovedThemeNameActionMask =
    ThemeNameActionBit(UiThemeNameAction::New) |
    ThemeNameActionBit(UiThemeNameAction::Load) |
    ThemeNameActionBit(UiThemeNameAction::Save) |
    ThemeNameActionBit(UiThemeNameAction::Rename);
static_assert(ThemeViewUi2Snapshot::ColorCount < 32U);
inline constexpr std::uint32_t kApprovedThemeEditableColorMask =
    (std::uint32_t{1} << ThemeViewUi2Snapshot::ColorCount) - 1U;

[[nodiscard]] constexpr bool
ThemeControllerCoversApprovedContract(const ThemeViewUi2Snapshot &snapshot) {
  return (snapshot.nameActionMask & kApprovedThemeNameActionMask) ==
             kApprovedThemeNameActionMask &&
         (snapshot.editableColorMask & kApprovedThemeEditableColorMask) ==
             kApprovedThemeEditableColorMask;
}

[[nodiscard]] constexpr UiThemeControllerFocus
AdaptLegacyThemeFocus(std::int16_t focusIndex) {
  switch (focusIndex) {
  case 0:
    return {.focus = ThemeViewUi2Focus::Name,
            .selectedColor = -1,
            .nameAction =
                static_cast<std::uint8_t>(UiThemeNameAction::Rename)};
  case 1:
    return {.focus = ThemeViewUi2Focus::Name,
            .selectedColor = -1,
            .nameAction =
                static_cast<std::uint8_t>(UiThemeNameAction::Load)};
  case 2:
    return {.focus = ThemeViewUi2Focus::Name,
            .selectedColor = -1,
            .nameAction =
                static_cast<std::uint8_t>(UiThemeNameAction::Save)};
  case 3:
    return {.focus = ThemeViewUi2Focus::Font};
  default:
    break;
  }

  constexpr std::int16_t firstColorComponent = 5;
  constexpr std::int16_t fieldsPerLegacyColor = 5;
  constexpr std::int16_t editableComponentsPerColor = 3;
  if (focusIndex < firstColorComponent)
    return {};

  const std::int16_t componentOffset = focusIndex - firstColorComponent;
  const std::int16_t legacyColor = componentOffset / fieldsPerLegacyColor;
  const std::int16_t component = componentOffset % fieldsPerLegacyColor;
  if (legacyColor < 0 ||
      static_cast<std::size_t>(legacyColor) >=
          kLegacyThemeColorToUi2.size() ||
      component >= editableComponentsPerColor) {
    return {};
  }

  const std::int8_t selectedColor = kLegacyThemeColorToUi2[legacyColor];
  if (selectedColor < 0)
    return {};
  return {.focus = ThemeViewUi2Focus::Color,
          .selectedColor = selectedColor,
          .nameAction = 0};
}

} // namespace ui2
