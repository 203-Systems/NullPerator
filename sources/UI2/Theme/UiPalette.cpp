/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Theme/UiPalette.h"

namespace ui2 {

UiPalette::UiPalette() {
  for (std::size_t index = 0; index < kColorCount; ++index) {
    SetRaw(static_cast<PaletteIndex>(index), {});
  }
  SetRaw(Index(UiColorToken::SurfaceBlack), {0x00, 0x00, 0x00});
  SetRaw(Index(UiColorToken::SurfaceField), {0x03, 0x07, 0x07});
  SetRaw(Index(UiColorToken::SurfaceBar), {0x0B, 0x17, 0x15});
  SetRaw(Index(UiColorToken::SurfaceBarDeep), {0x08, 0x12, 0x10});
  SetRaw(Index(UiColorToken::TextPrimary), {0xE8, 0xEE, 0xEB});
  SetRaw(Index(UiColorToken::TextMuted), {0x59, 0x64, 0x62});
  SetRaw(Index(UiColorToken::TextDim), {0x35, 0x40, 0x3E});
  SetRaw(Index(UiColorToken::CursorPrimary), {0x45, 0xDC, 0xE8});
  SetRaw(Index(UiColorToken::CursorRow), {0x15, 0x18, 0x1A});
  SetRaw(Index(UiColorToken::CursorInk), {0x04, 0x10, 0x11});
  SetRaw(Index(UiColorToken::PlaybackActive), {0x68, 0xE6, 0x9A});
  SetRaw(Index(UiColorToken::BatteryNormal), {0xE8, 0xEE, 0xEB});
  SetRaw(Index(UiColorToken::BatteryCharging), {0x00, 0xDC, 0x74});
  SetRaw(Index(UiColorToken::BatteryLow), {0xF0, 0x2E, 0x75});
  SetRaw(Index(UiColorToken::VuTrack), {0x07, 0x10, 0x0E});
  SetRaw(Index(UiColorToken::VuSafe), {0x00, 0xDC, 0x74});
  SetRaw(Index(UiColorToken::VuSafeLow), {0x00, 0xA9, 0x63});
  SetRaw(Index(UiColorToken::VuWarning), {0xF0, 0xCE, 0x00});
  SetRaw(Index(UiColorToken::VuPeak), {0xF0, 0x2E, 0x75});
  RebuildCoverage();
}

void UiPalette::Set(PaletteIndex index, Rgb888 color) {
  SetRaw(index, color);
  if (index < kThemeColorCount) RebuildCoverage();
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

void UiPalette::RebuildCoverage() {
  constexpr std::uint8_t kApprovedCoverage = 0x6B;
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
             Composite(sources[coverage], kApprovedCoverage,
                       Get(static_cast<PaletteIndex>(destination))));
      for (std::size_t level = 0; level < 3; ++level) {
        const PaletteIndex antialias = static_cast<PaletteIndex>(
            kAntialiasStart +
            (coverage * 3U + level) * kThemeColorCount +
            destination);
        SetRaw(antialias,
               CompositeQuarter(sources[coverage],
                                static_cast<std::uint8_t>(level + 1U),
                                Get(static_cast<PaletteIndex>(destination))));
      }
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
                                       PaletteIndex destination,
                                       std::uint8_t quarterCoverage) const {
  if (quarterCoverage == 0) return destination;
  if (quarterCoverage >= 4 || destination >= kThemeColorCount) {
    return coverage == UiCoverage::Cursor
               ? Index(UiColorToken::CursorPrimary)
               : Index(UiColorToken::PlaybackActive);
  }
  return static_cast<PaletteIndex>(
      kAntialiasStart +
      (static_cast<std::size_t>(coverage) * 3U + quarterCoverage - 1U) *
          kThemeColorCount +
      destination);
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
