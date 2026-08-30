/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Core/UiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui2 {

class UiAnimatedRect {
public:
  void Snap(RectI16 target, std::uint32_t nowMs) {
    initialized_ = true;
    x_.Start(target.x, target.x, nowMs, 1);
    y_.Start(target.y, target.y, nowMs, 1);
    width_.Start(target.width, target.width, nowMs, 1);
    height_.Start(target.height, target.height, nowMs, 1);
  }

  void Retarget(RectI16 target, std::uint32_t nowMs,
                std::uint16_t durationMs = 120) {
    if (!initialized_) {
      Snap(target, nowMs);
      return;
    }
    const RectI16 current = Sample(nowMs);
    x_.Start(current.x, target.x, nowMs, durationMs);
    y_.Start(current.y, target.y, nowMs, durationMs);
    width_.Start(current.width, target.width, nowMs, durationMs);
    height_.Start(current.height, target.height, nowMs, durationMs);
  }

  [[nodiscard]] RectI16 Sample(std::uint32_t nowMs) const {
    return {static_cast<std::int16_t>(x_.Sample(nowMs)),
            static_cast<std::int16_t>(y_.Sample(nowMs)),
            static_cast<std::int16_t>(width_.Sample(nowMs)),
            static_cast<std::int16_t>(height_.Sample(nowMs))};
  }

  [[nodiscard]] bool Active(std::uint32_t nowMs) const {
    return x_.Active(nowMs) || y_.Active(nowMs) || width_.Active(nowMs) ||
           height_.Active(nowMs);
  }

private:
  UiMotionTrack x_;
  UiMotionTrack y_;
  UiMotionTrack width_;
  UiMotionTrack height_;
  bool initialized_ = false;
};

enum class UiCursorRole : std::uint8_t {
  Content,
  TopMeta,
  BottomTrack,
  ChromeNavigation,
  Navigation,
  Count,
};

class UiCursorAnimatorSet {
public:
  void Snap(UiCursorRole role, RectI16 target, std::uint32_t nowMs) {
    Cursor(role).Snap(target, nowMs);
  }

  void Retarget(UiCursorRole role, RectI16 target, std::uint32_t nowMs,
                std::uint16_t durationMs = 120) {
    Cursor(role).Retarget(target, nowMs, durationMs);
  }

  [[nodiscard]] RectI16 Sample(UiCursorRole role,
                               std::uint32_t nowMs) const {
    return Cursor(role).Sample(nowMs);
  }

  [[nodiscard]] bool Active(UiCursorRole role, std::uint32_t nowMs) const {
    return Cursor(role).Active(nowMs);
  }

private:
  [[nodiscard]] UiAnimatedRect &Cursor(UiCursorRole role) {
    return cursors_[static_cast<std::size_t>(role)];
  }
  [[nodiscard]] const UiAnimatedRect &Cursor(UiCursorRole role) const {
    return cursors_[static_cast<std::size_t>(role)];
  }

  std::array<UiAnimatedRect, static_cast<std::size_t>(UiCursorRole::Count)>
      cursors_{};
};

} // namespace ui2
