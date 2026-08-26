/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Core/UiTypes.h"

#include <cstdint>

namespace ui2 {

enum class UiSlideDirection : std::uint8_t { Left, Right, Up, Down };

struct UiLayerOffsets {
  PointI16 outgoing;
  PointI16 incoming;
};

// Bars do not move with the page.  Their changed ink fades through the fixed
// bar background instead.  Keeping the two alpha values explicit lets the
// renderer use a clean fade-out/fade-in (with no ordered-dither noise) while
// preserving unchanged commands at full opacity.
struct UiBarCrossFadeSample {
  std::uint8_t outgoingAlpha = 255;
  std::uint8_t incomingAlpha = 0;
  bool incomingPhase = false;
  bool complete = false;
};

class UiBarCrossFadeTimeline {
public:
  static constexpr std::uint16_t kDurationMs = 140;
  static constexpr std::uint16_t kHalfDurationMs = kDurationMs / 2U;

  void Start(std::uint32_t nowMs) {
    startMs_ = nowMs;
    active_ = true;
  }

  void Reset() { active_ = false; }

  [[nodiscard]] UiBarCrossFadeSample Sample(std::uint32_t nowMs) const {
    if (!active_)
      return {.outgoingAlpha = 0,
              .incomingAlpha = 255,
              .incomingPhase = true,
              .complete = true};
    const std::uint32_t elapsed = nowMs - startMs_;
    if (elapsed >= kDurationMs)
      return {.outgoingAlpha = 0,
              .incomingAlpha = 255,
              .incomingPhase = true,
              .complete = true};

    if (elapsed < kHalfDurationMs) {
      const UnitQ16 time = static_cast<UnitQ16>(
          (static_cast<std::uint64_t>(elapsed) * 65'535U) /
          kHalfDurationMs);
      const std::uint32_t eased = EaseOutCubic(time);
      return {.outgoingAlpha = static_cast<std::uint8_t>(
                  255U - ((eased * 255U + 32'767U) >> 16U)),
              .incomingAlpha = 0,
              .incomingPhase = false,
              .complete = false};
    }

    const UnitQ16 time = static_cast<UnitQ16>(
        (static_cast<std::uint64_t>(elapsed - kHalfDurationMs) * 65'535U) /
        kHalfDurationMs);
    const std::uint32_t eased = EaseOutCubic(time);
    return {.outgoingAlpha = 0,
            .incomingAlpha = static_cast<std::uint8_t>(
                (eased * 255U + 32'767U) >> 16U),
            .incomingPhase = true,
            .complete = false};
  }

  [[nodiscard]] bool Active(std::uint32_t nowMs) const {
    return active_ && nowMs - startMs_ < kDurationMs;
  }

private:
  std::uint32_t startMs_ = 0;
  bool active_ = false;
};

class UiTransitionTimeline {
public:
  static constexpr std::uint16_t kContentDurationMs = 180;
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

  [[nodiscard]] UiLayerOffsets Content(std::uint32_t nowMs) const {
    return {{static_cast<std::int16_t>(outgoingX_.Sample(nowMs)),
             static_cast<std::int16_t>(outgoingY_.Sample(nowMs))},
            {static_cast<std::int16_t>(incomingX_.Sample(nowMs)),
             static_cast<std::int16_t>(incomingY_.Sample(nowMs))}};
  }

  [[nodiscard]] bool ContentActive(std::uint32_t nowMs) const {
    return outgoingX_.Active(nowMs) || outgoingY_.Active(nowMs) ||
           incomingX_.Active(nowMs) || incomingY_.Active(nowMs);
  }

private:
  UiMotionTrack outgoingX_;
  UiMotionTrack outgoingY_;
  UiMotionTrack incomingX_;
  UiMotionTrack incomingY_;
};

} // namespace ui2
