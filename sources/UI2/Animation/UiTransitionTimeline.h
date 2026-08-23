/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Animation/UiAnimatedRect.h"

#include <cstdint>

namespace ui2 {

enum class UiSlideDirection : std::uint8_t { Left, Right, Up, Down };

struct UiLayerOffsets {
  PointI16 outgoing;
  PointI16 incoming;
};

struct UiCrossfadeOpacity {
  UnitQ16 outgoing = 0;
  UnitQ16 incoming = 65'535;
};

class UiTransitionTimeline {
public:
  static constexpr std::uint16_t kContentDurationMs = 180;
  static constexpr std::uint16_t kBarFadeDurationMs = 120;
  static constexpr std::uint16_t kCursorDurationMs = 120;

  void StartContent(UiSlideDirection direction, std::uint32_t nowMs) {
    PointI16 entry{};
    switch (direction) {
    case UiSlideDirection::Left: entry.x = kScreenWidth; break;
    case UiSlideDirection::Right: entry.x = -kScreenWidth; break;
    case UiSlideDirection::Up: entry.y = kScreenHeight; break;
    case UiSlideDirection::Down: entry.y = -kScreenHeight; break;
    }
    outgoingX_.Start(0, -entry.x, nowMs, kContentDurationMs);
    outgoingY_.Start(0, -entry.y, nowMs, kContentDurationMs);
    incomingX_.Start(entry.x, 0, nowMs, kContentDurationMs);
    incomingY_.Start(entry.y, 0, nowMs, kContentDurationMs);
  }

  void StartBarFade(std::uint32_t nowMs) {
    barFade_.Start(0, 65'535, nowMs, kBarFadeDurationMs);
  }

  [[nodiscard]] UiLayerOffsets Content(std::uint32_t nowMs) const {
    return {{static_cast<std::int16_t>(outgoingX_.Sample(nowMs)),
             static_cast<std::int16_t>(outgoingY_.Sample(nowMs))},
            {static_cast<std::int16_t>(incomingX_.Sample(nowMs)),
             static_cast<std::int16_t>(incomingY_.Sample(nowMs))}};
  }

  [[nodiscard]] UiCrossfadeOpacity BarFade(std::uint32_t nowMs) const {
    const auto incoming = static_cast<UnitQ16>(barFade_.Sample(nowMs));
    return {static_cast<UnitQ16>(65'535U - incoming), incoming};
  }

  [[nodiscard]] bool ContentActive(std::uint32_t nowMs) const {
    return outgoingX_.Active(nowMs) || outgoingY_.Active(nowMs) ||
           incomingX_.Active(nowMs) || incomingY_.Active(nowMs);
  }

  [[nodiscard]] bool BarFadeActive(std::uint32_t nowMs) const {
    return barFade_.Active(nowMs);
  }

  UiCursorAnimatorSet &Cursors() { return cursors_; }
  const UiCursorAnimatorSet &Cursors() const { return cursors_; }

private:
  UiMotionTrack outgoingX_;
  UiMotionTrack outgoingY_;
  UiMotionTrack incomingX_;
  UiMotionTrack incomingY_;
  UiMotionTrack barFade_;
  UiCursorAnimatorSet cursors_;
};

} // namespace ui2
