/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Core/UiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

// Stable semantic indices are part of the UI2 presenter contract. Page code
// never names hues and user themes only replace these semantic roles.
enum class UiColorToken : PaletteIndex {
  SurfaceBackground = 0,
  SurfaceTopBar,
  SurfaceBottomBar,
  TextNormal,
  TextDim,
  TextHighlighted,
  TextColored,
  CursorPrimary,
  CursorRow,
  PlaybackActive,
  SystemInfo,
  SystemWarning,
  SystemError,
  BatteryNormal,
  BatteryCharging,
  BatteryLow,
  VuSafe,
  VuWarning,
  VuPeak,

  // Generated implementation colors. These are not persisted theme fields
  // and never appear in Theme UI.
  DerivedTextFaint,
  DerivedVuTrack,
  DerivedVuSafeLow,
  Count,
};

enum class UiCoverage : std::uint8_t { Cursor = 0, Playback = 1 };

class UiPalette {
public:
  static constexpr std::size_t kColorCount = 256;
  static constexpr std::size_t kUserColorCount =
      static_cast<std::size_t>(UiColorToken::DerivedTextFaint);
  // Element-owned dynamic ramps may use this bank. The stable theme and
  // generated cursor/waveform colors live below it.
  static constexpr PaletteIndex kFirstDynamicIndex = 96;

  UiPalette();

  void Set(PaletteIndex index, Rgb888 color);
  void Set(UiColorToken token, Rgb888 color) {
    Set(static_cast<PaletteIndex>(token), color);
  }
  [[nodiscard]] Rgb888 Get(PaletteIndex index) const;
  [[nodiscard]] PaletteIndex Index(UiColorToken token) const {
    return static_cast<PaletteIndex>(token);
  }
  [[nodiscard]] std::uint16_t Rgb565(PaletteIndex index) const;
  [[nodiscard]] std::uint32_t Rgba8888(PaletteIndex index) const;
  [[nodiscard]] PaletteIndex CoverageIndex(UiCoverage coverage,
                                            PaletteIndex destination) const;
  [[nodiscard]] PaletteIndex AntialiasIndex(UiCoverage coverage,
                                             std::uint8_t quarterCoverage) const;

  [[nodiscard]] static constexpr std::uint16_t PackRgb565(Rgb888 color) {
    return static_cast<std::uint16_t>(
        ((static_cast<std::uint16_t>(color.red) & 0xF8U) << 8U) |
        ((static_cast<std::uint16_t>(color.green) & 0xFCU) << 3U) |
        (static_cast<std::uint16_t>(color.blue) >> 3U));
  }

private:
  static constexpr std::size_t kThemeColorCount =
      static_cast<std::size_t>(UiColorToken::Count);
  static constexpr PaletteIndex kCoverageStart = 32;
  static constexpr PaletteIndex kElementAntialiasStart =
      kCoverageStart + 2 * kThemeColorCount;
  static_assert(kThemeColorCount <= kCoverageStart);
  static_assert(kElementAntialiasStart + 2 * 3 <= kFirstDynamicIndex);

  void SetRaw(PaletteIndex index, Rgb888 color);
  void RebuildDerivedColors();
  [[nodiscard]] static Rgb888 Composite(Rgb888 source, std::uint8_t alpha,
                                        Rgb888 destination);
  [[nodiscard]] static Rgb888 CompositeQuarter(Rgb888 source,
                                               std::uint8_t quarters,
                                               Rgb888 destination);

  std::array<Rgb888, kColorCount> colors_{};
  std::array<std::uint16_t, kColorCount> rgb565_{};
  std::array<std::array<PaletteIndex, kThemeColorCount>, 2> coverage_{};
};

} // namespace ui2
