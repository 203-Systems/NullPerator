/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

// Tracks physical button-down events consumed by a ModalView. A modal may
// dismiss synchronously on key-down, so those bits remain hidden from the UI2
// base page until their matching key-up events arrive.
class Ui2ModalInputGate final {
public:
  void OnButtonDown(std::uint16_t bit, bool modalOwnsInput) {
    if (modalOwnsInput)
      consumedMask_ = static_cast<std::uint16_t>(consumedMask_ | bit);
  }

  void OnButtonUp(std::uint16_t bit) {
    consumedMask_ = static_cast<std::uint16_t>(consumedMask_ & ~bit);
  }

  [[nodiscard]] std::uint16_t EffectiveMask(std::uint16_t physicalMask,
                                             bool modalActive) const {
    if (modalActive)
      return 0U;
    return static_cast<std::uint16_t>(physicalMask & ~consumedMask_);
  }

  // ModalView keeps receiving the complete physical chord. Once it is gone,
  // the base view must not observe any modal-owned key until that key is up.
  [[nodiscard]] std::uint16_t DispatchMask(std::uint16_t physicalMask,
                                           bool modalActive) const {
    if (modalActive)
      return physicalMask;
    return static_cast<std::uint16_t>(physicalMask & ~consumedMask_);
  }

private:
  std::uint16_t consumedMask_ = 0U;
};

static_assert(sizeof(Ui2ModalInputGate) == sizeof(std::uint16_t));
