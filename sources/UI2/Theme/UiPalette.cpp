/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Theme/UiPalette.h"

#include "UI2/Elements/UiCursorElement.h"
#include "UI2/Elements/UiVuElement.h"

namespace ui2 {

UiPalette::UiPalette() {
  for (std::size_t index = 0; index < kColorCount; ++index) {
    SetRaw(static_cast<PaletteIndex>(index), {});
  }
  SetRaw(Index(UiColorToken::SurfaceBackground), {0x03, 0x07, 0x07});
  SetRaw(Index(UiColorToken::SurfaceTopBar), {0x08, 0x12, 0x10});
  SetRaw(Index(UiColorToken::SurfaceBottomBar), {0x08, 0x12, 0x10});
  SetRaw(Index(UiColorToken::TextNormal), {0xE8, 0xEE, 0xEB});
  SetRaw(Index(UiColorToken::TextDim), {0x59, 0x64, 0x62});
  SetRaw(Index(UiColorToken::TextHighlighted), {0x04, 0x10, 0x11});
  SetRaw(Index(UiColorToken::TextColored), {0x45, 0xDC, 0xE8});
  SetRaw(Index(UiColorToken::CursorPrimary), {0x45, 0xDC, 0xE8});
  SetRaw(Index(UiColorToken::CursorRow), {0x15, 0x18, 0x1A});
  SetRaw(Index(UiColorToken::SelectionActive), {0x1A, 0x33, 0x35});
  SetRaw(Index(UiColorToken::PlaybackActive), {0x68, 0xE6, 0x9A});
  SetRaw(Index(UiColorToken::SystemInfo), {0x00, 0xDC, 0x74});
  SetRaw(Index(UiColorToken::SystemWarning), {0xF0, 0xCE, 0x00});
  SetRaw(Index(UiColorToken::SystemError), {0xF0, 0x2E, 0x75});
  SetRaw(Index(UiColorToken::BatteryNormal), {0xE8, 0xEE, 0xEB});
  SetRaw(Index(UiColorToken::BatteryCharging), {0x00, 0xDC, 0x74});
  SetRaw(Index(UiColorToken::BatteryLow), {0xF0, 0x2E, 0x75});
  SetRaw(Index(UiColorToken::VuSafe), {0x00, 0xDC, 0x74});
  SetRaw(Index(UiColorToken::VuWarning), {0xF0, 0xCE, 0x00});
  SetRaw(Index(UiColorToken::VuPeak), {0xF0, 0x2E, 0x75});
  RebuildDerivedColors();
}

void UiPalette::Set(PaletteIndex index, Rgb888 color) {
  SetRaw(index, color);
  if (index < kUserColorCount) RebuildDerivedColors();
}

void UiPalette::SetUserColors(
    const std::array<Rgb888, kUserColorCount> &colors) {
  for (std::size_t index = 0; index < colors.size(); ++index)
    SetRaw(static_cast<PaletteIndex>(index), colors[index]);
  RebuildDerivedColors();
}

void UiPalette::SetRaw(PaletteIndex index, Rgb888 color) {
  colors_[index] = color;
  rgb565_[index] = PackRgb565(color);
}

Rgb888 UiPalette::Composite(Rgb888 source, std::uint8_t alpha,
                            Rgb888 destination) {
  const auto channel = [alpha](std::uint8_t sourceValue,
                               std::uint8_t destinationValue) {
    const std::uint32_t value = static_cast<std::uint32_t>(sourceValue) * alpha +
                                static_cast<std::uint32_t>(destinationValue) *
                                    (255U - alpha);
    return static_cast<std::uint8_t>((value + 127U) / 255U);
  };
  return {channel(source.red, destination.red),
          channel(source.green, destination.green),
          channel(source.blue, destination.blue)};
}

Rgb888 UiPalette::CompositeQuarter(Rgb888 source, std::uint8_t quarters,
                                   Rgb888 destination) {
  const auto channel = [quarters](std::uint8_t sourceValue,
                                  std::uint8_t destinationValue) {
    const std::uint16_t value =
        static_cast<std::uint16_t>(sourceValue) * quarters +
        static_cast<std::uint16_t>(destinationValue) * (4U - quarters);
    return static_cast<std::uint8_t>((value + 2U) / 4U);
  };
  return {channel(source.red, destination.red),
          channel(source.green, destination.green),
          channel(source.blue, destination.blue)};
}

void UiPalette::RebuildDerivedColors() {
  SetRaw(Index(UiColorToken::DerivedTextFaint),
         Composite(Get(Index(UiColorToken::TextDim)), 153,
                   Get(Index(UiColorToken::SurfaceBackground))));
  SetRaw(Index(UiColorToken::DerivedVuTrack),
         Composite(Get(Index(UiColorToken::VuSafe)),
                   UiVuElement::kTrackAlpha,
                   Get(Index(UiColorToken::SurfaceBackground))));
  SetRaw(Index(UiColorToken::DerivedVuSafeLow),
         Composite(Get(Index(UiColorToken::VuSafe)),
                   UiVuElement::kSafeLowAlpha,
                   Get(Index(UiColorToken::SurfaceBackground))));
  SetRaw(Index(UiColorToken::DerivedCursorRowCorner),
         Composite(Get(Index(UiColorToken::CursorRow)),
                   UiCursorElement::kCornerAlpha,
                   Get(Index(UiColorToken::SurfaceBackground))));
  SetRaw(Index(UiColorToken::DerivedSelectionCorner),
         Composite(Get(Index(UiColorToken::SelectionActive)),
                   UiCursorElement::kCornerAlpha,
                   Get(Index(UiColorToken::SurfaceBackground))));
  SetRaw(Index(UiColorToken::DerivedPlaybackMuted),
         Composite(Get(Index(UiColorToken::PlaybackActive)),
                   UiCursorElement::kCornerAlpha,
                   Get(Index(UiColorToken::SurfaceBackground))));

  // The cursor corner coverage belongs to the cursor element. It is generated
  // once after a theme edit and cached as indexed colors for the ESP32 path.
  const std::array<Rgb888, 2> sources{
      Get(Index(UiColorToken::CursorPrimary)),
      Get(Index(UiColorToken::PlaybackActive))};
  for (std::size_t coverage = 0; coverage < coverage_.size(); ++coverage) {
    for (std::size_t destination = 0; destination < kThemeColorCount;
         ++destination) {
      const PaletteIndex derived = static_cast<PaletteIndex>(
          kCoverageStart + coverage * kThemeColorCount + destination);
      coverage_[coverage][destination] = derived;
      SetRaw(derived,
             Composite(sources[coverage], UiCursorElement::kCornerAlpha,
                       Get(static_cast<PaletteIndex>(destination))));
    }
    // Sparse waveform coverage is always composited over the VU track. Its
    // three coverage levels are element cache entries, not theme settings.
    for (std::size_t level = 0; level < 3; ++level) {
      const PaletteIndex antialias = static_cast<PaletteIndex>(
          kElementAntialiasStart + coverage * 3U + level);
      SetRaw(antialias,
             CompositeQuarter(sources[coverage],
                              UiCursorElement::kWaveformCoverageQuarters[level],
                              Get(Index(UiColorToken::DerivedVuTrack))));
    }
  }
}

PaletteIndex UiPalette::CoverageIndex(UiCoverage coverage,
                                      PaletteIndex destination) const {
  if (destination < kThemeColorCount) {
    return coverage_[static_cast<std::size_t>(coverage)][destination];
  }
  return coverage == UiCoverage::Cursor
             ? Index(UiColorToken::CursorPrimary)
             : Index(UiColorToken::PlaybackActive);
}

PaletteIndex UiPalette::AntialiasIndex(UiCoverage coverage,
                                       std::uint8_t quarterCoverage) const {
  if (quarterCoverage == 0)
    return Index(UiColorToken::DerivedVuTrack);
  if (quarterCoverage >= 4) {
    return coverage == UiCoverage::Cursor
               ? Index(UiColorToken::CursorPrimary)
               : Index(UiColorToken::PlaybackActive);
  }
  return static_cast<PaletteIndex>(
      kElementAntialiasStart +
      static_cast<std::size_t>(coverage) * 3U + quarterCoverage - 1U);
}

Rgb888 UiPalette::Get(PaletteIndex index) const { return colors_[index]; }

std::uint16_t UiPalette::Rgb565(PaletteIndex index) const {
  return rgb565_[index];
}

std::uint32_t UiPalette::Rgba8888(PaletteIndex index) const {
  const Rgb888 color = colors_[index];
  return (static_cast<std::uint32_t>(color.red) << 24U) |
         (static_cast<std::uint32_t>(color.green) << 16U) |
         (static_cast<std::uint32_t>(color.blue) << 8U) | 0xFFU;
}

} // namespace ui2
