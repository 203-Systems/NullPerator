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
  static constexpr std::size_t ColorCount = 19;

  std::array<char, NameCapacity> name{};
  std::array<std::uint32_t, ColorCount> colors{};
  ThemeViewUi2Focus focus = ThemeViewUi2Focus::Unknown;
  std::int8_t selectedColor = -1;
  std::uint8_t nameAction = 0;
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
