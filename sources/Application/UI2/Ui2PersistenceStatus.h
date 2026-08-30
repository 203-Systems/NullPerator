/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>
#include <type_traits>

namespace ui2 {

// A synchronous persistence call cannot expose its busy state to the retained
// renderer unless at least one frame is presented before the call begins. This
// tiny state machine is the typed boundary between that frame and the following
// blocking write; it deliberately carries no persistence policy or text.
class Ui2PersistenceStatus final {
public:
  void BeginSaving() noexcept { phase_ = Phase::AwaitingPresentation; }

  void MarkPresented() noexcept {
    if (phase_ == Phase::AwaitingPresentation)
      phase_ = Phase::ReadyToPersist;
  }

  void FinishSaving() noexcept { phase_ = Phase::Idle; }

  [[nodiscard]] bool Saving() const noexcept { return phase_ != Phase::Idle; }
  [[nodiscard]] bool ReadyToPersist() const noexcept {
    return phase_ == Phase::ReadyToPersist;
  }

private:
  enum class Phase : std::uint8_t {
    Idle,
    AwaitingPresentation,
    ReadyToPersist,
  };

  Phase phase_ = Phase::Idle;
};

static_assert(std::is_trivially_copyable_v<Ui2PersistenceStatus>);
static_assert(sizeof(Ui2PersistenceStatus) == 1U,
              "persistence presentation state must stay embedded-friendly");

} // namespace ui2
