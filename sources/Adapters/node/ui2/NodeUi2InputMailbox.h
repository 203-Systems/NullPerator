/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace node::ui2 {

// The producer publishes only physical state. Edge history and repeat debt are
// retained inside this fixed-capacity accumulator until the UI2 application
// task drains them, so a slow LCD transfer cannot turn a release into a stuck
// key. The platform supplies the short cross-core critical section; this type
// deliberately has no FreeRTOS dependency and is host-testable.
class InputMailbox final {
public:
  static constexpr std::uint32_t kPressKillMs = 5U;
  static constexpr std::uint32_t kRepeatDelayMs = 500U;
  static constexpr std::uint32_t kRepeatPeriodMs = 75U;
  // Catch-up repeats are intentionally bounded: replaying seconds of stale
  // cursor movement after a blocked storage/LCD operation is worse than
  // dropping excess repeat pulses. Physical release edges remain independent.
  static constexpr std::uint8_t kMaxRepeatDebt = 8U;

  struct Event final {
    TrackerAction action = TrackerAction::Left;
    std::uint8_t count = 0U;
    bool pressed = false;
    bool repeat = false;
  };

  // One action can contribute at most release+press, followed by one
  // aggregated repeat record for each direction. Reserved actions are never
  // emitted, so 24 entries leaves a checked margin without dynamic storage.
  struct Batch final {
    static constexpr std::size_t kCapacity = 32U;

    [[nodiscard]] bool Push(TrackerAction action, bool pressed,
                            std::uint8_t count = 1U,
                            bool repeat = false);

    std::array<Event, kCapacity> events{};
    std::size_t size = 0U;
    std::uint16_t heldMask = 0U;
    std::uint32_t sampleTimestampMs = 0U;
    bool headphoneConnected = false;
    bool headphoneChanged = false;
    bool hasSample = false;
  };

  // Called by the 10 ms input task. Releases are accepted immediately;
  // presses observed inside the 5 ms transition-kill window remain candidates
  // until a later sample confirms them. This keeps the legacy timing while
  // making an observed release impossible to drop.
  void PublishSample(std::uint16_t physicalHeldMask,
                     bool headphoneConnected, std::uint32_t nowMs);

  // Called by the sole UI2 application task. Events are ordered as releases,
  // modifier presses, direction/ordinary presses, then aggregated repeats.
  // A press+release completed entirely between drains is emitted as a compact
  // tap after the priority phases rather than silently discarded.
  [[nodiscard]] Batch Drain();

  [[nodiscard]] std::uint16_t LatestPhysicalHeldMask() const {
    return latestPhysicalHeldMask_;
  }
  [[nodiscard]] std::uint16_t AcceptedHeldMask() const {
    return acceptedHeldMask_;
  }
  [[nodiscard]] bool HasPublishedSample() const { return initialized_; }
  [[nodiscard]] bool HasPending() const;

private:
  static constexpr std::uint16_t kDirectionMask =
      TrackerActionBit(TrackerAction::Left) |
      TrackerActionBit(TrackerAction::Down) |
      TrackerActionBit(TrackerAction::Right) |
      TrackerActionBit(TrackerAction::Up);
  static constexpr std::uint16_t kModifierMask =
      TrackerActionBit(TrackerAction::Shift) |
      TrackerActionBit(TrackerAction::Option) |
      TrackerActionBit(TrackerAction::Edit);
  static constexpr std::uint16_t kSupportedMask =
      kDirectionMask | kModifierMask |
      TrackerActionBit(TrackerAction::Play) |
      TrackerActionBit(TrackerAction::Power);

  [[nodiscard]] static constexpr bool TimeReached(std::uint32_t nowMs,
                                                  std::uint32_t targetMs) {
    return static_cast<std::int32_t>(nowMs - targetMs) >= 0;
  }
  [[nodiscard]] static constexpr std::uint16_t Bit(TrackerAction action) {
    return TrackerActionBit(action);
  }
  static constexpr std::size_t DirectionIndex(TrackerAction action) {
    return static_cast<std::size_t>(action);
  }

  void AcceptPresses(std::uint16_t mask, std::uint32_t nowMs);
  void AcceptReleases(std::uint16_t mask, std::uint32_t nowMs);
  void AccumulateRepeats(std::uint32_t nowMs);
  static void SaturatingAdd(std::uint8_t &value, std::uint32_t increment);

  std::array<std::uint32_t, 4U> nextRepeatMs_{};
  std::array<std::uint8_t, 4U> repeatDebt_{};
  std::uint16_t latestPhysicalHeldMask_ = 0U;
  std::uint16_t acceptedHeldMask_ = 0U;
  std::uint16_t deliveredHeldMask_ = 0U;
  std::uint16_t pendingPressedMask_ = 0U;
  std::uint16_t pendingReleasedMask_ = 0U;
  std::uint32_t lastAcceptedTransitionMs_ = 0U;
  std::uint32_t latestSampleMs_ = 0U;
  bool latestHeadphoneConnected_ = false;
  bool deliveredHeadphoneConnected_ = false;
  bool initialized_ = false;
  bool headphoneDelivered_ = false;
  bool samplePending_ = false;
};

static_assert(std::is_trivially_copyable_v<InputMailbox::Event>);
static_assert(sizeof(InputMailbox::Batch) <= 192U);
static_assert(sizeof(InputMailbox) <= 64U,
              "the cross-core mailbox must remain a small fixed object");

} // namespace node::ui2
