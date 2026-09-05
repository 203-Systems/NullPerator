/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "Config.h"
#include <array>
#include <cstdint>
namespace ThemeDocument {
inline constexpr std::array<const char *, Config::SemanticThemeColorCount>
    kSemanticThemeColorKeys{{
        "surface.bg",
        "surface.top_bar",
        "surface.bottom_bar",
        "text.normal",
        "text.dim",
        "text.highlighted",
        "text.colored",
        "cursor.primary",
        "cursor.row",
        "selection.active",
        "playback.active",
        "system.info",
        "system.warning",
        "system.error",
        "battery.normal",
        "battery.charging",
        "battery.low",
        "vu.safe",
        "vu.warning",
        "vu.peak",
    }};

inline constexpr std::uint32_t kAllSemanticThemeColors =
    (std::uint32_t{1} << Config::SemanticThemeColorCount) - 1U;

struct ThemeLoadState {
  Config::SemanticThemeColors semanticColors{};
  std::uint32_t semanticMask = 0U;
  int font = 0;
  bool hasFont = false;
};

bool ParseThemeColor(const char *text, std::uint32_t &color);
bool IsLegacyThemeColorId(FourCC id);
bool ParseThemeDocument(PersistencyDocument &doc, ThemeLoadState &state);
void ApplyThemeState(Config &config, const ThemeLoadState &state);
} // namespace ThemeDocument
