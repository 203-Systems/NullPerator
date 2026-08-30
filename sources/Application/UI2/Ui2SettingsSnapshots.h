/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class ThemeViewUi2Focus : std::uint8_t {
  Name,
  Color,
  Font,
  Unknown,
};

// Lightweight owned settings snapshots. Keeping these definitions outside
// ThemeView.h lets host tests and the shared runtime use them without pulling
// the legacy GUI/controller dependency graph into renderer-only builds.
struct ThemeViewUi2Snapshot {
  static constexpr std::size_t NameCapacity = 17;
  static constexpr std::size_t ColorCount = 20;

  std::array<char, NameCapacity> name{};
  std::array<std::uint32_t, ColorCount> colors{};
  // A compatibility snapshot may mirror one legacy color into several UI2
  // slots. These masks describe what the real controller can actually edit;
  // they must not be inferred from colors[] being populated.
  std::uint32_t editableColorMask = 0;
  ThemeViewUi2Focus focus = ThemeViewUi2Focus::Unknown;
  std::int8_t selectedColor = -1;
  std::uint8_t nameAction = 0;
  std::uint8_t nameActionMask = 0;
  bool colorsValid = false;

  [[nodiscard]] constexpr bool IsColorEditable(std::size_t index) const {
    return index < ColorCount &&
           (editableColorMask & (std::uint32_t{1} << index)) != 0U;
  }

  [[nodiscard]] constexpr bool HasNameAction(std::uint8_t action) const {
    return action < 8U &&
           (nameActionMask & static_cast<std::uint8_t>(1U << action)) != 0U;
  }
};

struct FontViewUi2Snapshot {
  static constexpr std::size_t FontCapacity = 41;

  std::array<char, FontCapacity> font{};
  std::uint8_t current = 0;
  std::uint8_t count = 0;
  bool wrap = false;
};

static_assert(std::is_trivially_copyable_v<ThemeViewUi2Snapshot>);
static_assert(std::is_trivially_copyable_v<FontViewUi2Snapshot>);
static_assert(sizeof(ThemeViewUi2Snapshot) <= 128U);
static_assert(sizeof(FontViewUi2Snapshot) <= 48U);
