/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Render/UiVuGradient.h"

#include <array>
#include <cstddef>

namespace ui2 {
namespace {

struct Stop {
  std::uint8_t percent;
  UiColorToken color;
};

constexpr std::array<Stop, 7> kStops{{
    {0, UiColorToken::VuPeak},
    {6, UiColorToken::VuPeak},
    {10, UiColorToken::VuWarning},
    {20, UiColorToken::VuWarning},
    {27, UiColorToken::VuSafe},
    {62, UiColorToken::VuSafe},
    {100, UiColorToken::VuSafeLow},
}};

std::uint8_t LerpChannel(std::uint8_t from, std::uint8_t to,
                         std::uint32_t numerator,
                         std::uint32_t denominator) {
  const std::uint32_t weighted =
      static_cast<std::uint32_t>(from) * (denominator - numerator) +
      static_cast<std::uint32_t>(to) * numerator;
  return static_cast<std::uint8_t>((weighted + denominator / 2U) /
                                   denominator);
}

} // namespace

bool UiVuGradient::Configure(UiPalette &palette, std::uint16_t height) {
  if (height == 0 || height > kMaximumHeight) return false;
  const std::uint32_t coordinateDenominator = 2U * height;
  for (std::uint16_t row = 0; row < height; ++row) {
    const std::uint32_t coordinate = 100U * (2U * row + 1U);
    std::size_t upper = 1;
    while (upper < kStops.size() &&
           coordinate > kStops[upper].percent * coordinateDenominator) {
      ++upper;
    }
    if (upper >= kStops.size()) upper = kStops.size() - 1;
    const Stop lowerStop = kStops[upper - 1];
    const Stop upperStop = kStops[upper];
    const std::uint32_t lower =
        lowerStop.percent * coordinateDenominator;
    const std::uint32_t denominator =
        (upperStop.percent - lowerStop.percent) * coordinateDenominator;
    const std::uint32_t numerator =
        coordinate > lower ? coordinate - lower : 0;
    const Rgb888 from = palette.Get(palette.Index(lowerStop.color));
    const Rgb888 to = palette.Get(palette.Index(upperStop.color));
    palette.Set(IndexAt(row),
                {LerpChannel(from.red, to.red, numerator, denominator),
                 LerpChannel(from.green, to.green, numerator, denominator),
                 LerpChannel(from.blue, to.blue, numerator, denominator)});
  }
  return true;
}

} // namespace ui2
