/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "UI2/Chrome/UiChromeModel.h"

#include <cstdint>

namespace ui2 {

// Grid data and its clipboard are durable editing state, not modal UI modes.
// This controller owns only short-lived Bottom Bar acknowledgements for grid
// operations such as copy, paste, and interpolation.
class Ui2ClipboardNoticeController final {
public:
  static constexpr std::uint32_t DurationMs = 2000U;

  [[nodiscard]] bool Active() const { return active_; }
  [[nodiscard]] const UiClipboardBarModel &Model() const { return model_; }

  void ShowCopied(std::uint8_t width, std::uint8_t height,
                  std::uint32_t nowMs) {
    Show(UiClipboardBarModel::Notice::Copied, width, height, nowMs);
  }

  void ShowPasted(std::uint8_t width, std::uint8_t height,
                  std::uint32_t nowMs) {
    Show(UiClipboardBarModel::Notice::Pasted, width, height, nowMs);
  }

  void ShowInterpolated(std::uint8_t height, std::uint32_t nowMs) {
    Show(UiClipboardBarModel::Notice::Interpolated, 1U, height, nowMs);
  }

  // Returns true only when expiry changes visible presentation state.
  bool Tick(std::uint32_t nowMs) {
    if (!active_ || nowMs - shownAtMs_ < DurationMs)
      return false;
    Clear();
    return true;
  }

  void Clear() { active_ = false; }

private:
  void Show(UiClipboardBarModel::Notice notice, std::uint8_t width,
            std::uint8_t height, std::uint32_t nowMs) {
    model_ = {.width = width, .height = height, .notice = notice};
    shownAtMs_ = nowMs;
    active_ = width != 0U && height != 0U;
  }

  UiClipboardBarModel model_{};
  std::uint32_t shownAtMs_ = 0U;
  bool active_ = false;
};

} // namespace ui2
