/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Animation/UiTransitionTimeline.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiFrameScene.h"
#include "UI2/Theme/UiPalette.h"

#include <array>
#include <cstdint>

namespace ui2 {

// Fixed-capacity page-bar transition state.  The current surface remains the
// outgoing visual source, so Begin() can interrupt an in-flight fade without
// rebuilding a large RGB framebuffer or snapping back to a logical scene.
class UiPageBarTransition {
public:
  static constexpr std::size_t kColorsPerBar = 80;
  static constexpr PaletteIndex kTopFirstColor =
      UiPalette::kFirstDynamicIndex;
  static constexpr PaletteIndex kBottomFirstColor =
      static_cast<PaletteIndex>(kTopFirstColor + kColorsPerBar);
  static_assert(kBottomFirstColor + kColorsPerBar == UiPalette::kColorCount);

  void Begin(const UiFrameScene &outgoing, const UiPalette &palette,
             std::uint32_t nowMs);
  void Reset();

  [[nodiscard]] bool TopActive(std::uint32_t nowMs) const;
  [[nodiscard]] bool BottomActive(std::uint32_t nowMs) const;

private:
  struct SourceColor {
    Rgb888 color{};
    PaletteIndex index = 0;
  };

  struct BarState {
    // A semantic command fingerprint is enough to preserve unchanged glyphs.
    // We deliberately do not retain a second pair of 1.1 KiB bar command
    // buffers; unmatched outgoing damage may be conservatively widened to
    // outgoingBounds without changing pixels outside this bar.
    std::array<std::uint64_t, 64> outgoingHashes{};
    std::uint64_t incomingChanged = 0;
    std::array<SourceColor, kColorsPerBar> sourceColors{};
    UiBarCrossFadeTimeline timeline{};
    RectI16 outgoingClip{};
    RectI16 outgoingBounds{};
    RectI16 carryDamage{};
    RectI16 damage{};
    std::uint32_t startMs = 0;
    UiColorToken outgoingBackground = UiColorToken::SurfaceBackground;
    std::uint8_t sourceColorCount = 0;
    std::uint8_t outgoingCount = 0;
    bool outgoingVisible = false;
    bool pending = false;
    bool prepared = false;
  };

  void BeginBar(BarState &bar, UiCommandStream outgoing, RectI16 clip,
                UiColorToken background, bool visible,
                UiTextCaseMode textCase,
                std::uint32_t nowMs);

  std::array<Rgb888, UiPalette::kColorCount - UiPalette::kFirstDynamicIndex>
      dynamicColorsAtStart_{};
  BarState top_{};
  BarState bottom_{};
  // The content window may shrink when either endpoint owns a bar, but it must
  // never expand again during an interrupted transition and scroll bar pixels.
  std::int16_t contentClipTop_ = 0;
  std::int16_t contentClipBottom_ = 0;

  friend class UiFrameRenderer;
};

static_assert(sizeof(UiPageBarTransition) < 4'000);

class UiFrameRenderer {
public:
  static void RenderStatic(const UiFrameScene &scene,
                           UiIndexedSurface &surface,
                           const UiPalette &palette);
  static void RenderRegion(const UiFrameScene &scene,
                           UiIndexedSurface &surface,
                           const UiPalette &palette, RectI16 region);
  static void AdvancePageTransition(
      const UiFrameScene &incoming, UiLayerOffsets offsets,
      UiLayerOffsets previousOffsets, std::uint32_t nowMs,
      UiPageBarTransition &bars, UiIndexedSurface &surface,
      UiPalette &palette);
};

} // namespace ui2
