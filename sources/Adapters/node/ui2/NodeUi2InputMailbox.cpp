/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/node/ui2/NodeUi2InputMailbox.h"

#include <algorithm>

namespace node::ui2 {
namespace {

constexpr std::array<TrackerAction, 3U> kModifierOrder = {
    TrackerAction::Shift, TrackerAction::Option, TrackerAction::Edit};
constexpr std::array<TrackerAction, 6U> kOrdinaryOrder = {
    TrackerAction::Left,  TrackerAction::Down, TrackerAction::Right,
    TrackerAction::Up,    TrackerAction::Play, TrackerAction::Power};
constexpr std::array<TrackerAction, 9U> kAllActionOrder = {
    TrackerAction::Left,  TrackerAction::Down,   TrackerAction::Right,
    TrackerAction::Up,    TrackerAction::Shift,  TrackerAction::Option,
    TrackerAction::Edit,  TrackerAction::Play,   TrackerAction::Power};
constexpr std::array<TrackerAction, 4U> kDirectionOrder = {
    TrackerAction::Left, TrackerAction::Down, TrackerAction::Right,
    TrackerAction::Up};

} // namespace

bool InputMailbox::Batch::Push(TrackerAction action, bool pressed,
                               std::uint8_t count, bool repeat) {
  if (count == 0U)
    return true;
  if (size >= events.size())
    return false;
  events[size++] = {.action = action,
                    .count = count,
                    .pressed = pressed,
                    .repeat = repeat};
  return true;
}

void InputMailbox::SaturatingAdd(std::uint8_t &value,
                                 std::uint32_t increment) {
  const std::uint32_t sum = static_cast<std::uint32_t>(value) + increment;
  value = static_cast<std::uint8_t>(
      std::min<std::uint32_t>(sum, kMaxRepeatDebt));
}

void InputMailbox::AcceptPresses(std::uint16_t mask, std::uint32_t nowMs) {
  mask &= static_cast<std::uint16_t>(kSupportedMask & ~acceptedHeldMask_);
  if (mask == 0U)
    return;

  acceptedHeldMask_ |= mask;
  pendingPressedMask_ |= mask;
  lastAcceptedTransitionMs_ = nowMs;
  for (const TrackerAction action : kDirectionOrder) {
    const std::uint16_t bit = Bit(action);
    if ((mask & bit) == 0U)
      continue;
    const std::size_t index = DirectionIndex(action);
    repeatDebt_[index] = 0U;
    nextRepeatMs_[index] = nowMs + kRepeatDelayMs;
  }
}

void InputMailbox::AcceptReleases(std::uint16_t mask, std::uint32_t nowMs) {
  mask &= acceptedHeldMask_;
  if (mask == 0U)
    return;

  acceptedHeldMask_ &= static_cast<std::uint16_t>(~mask);
  pendingReleasedMask_ |= mask;
  lastAcceptedTransitionMs_ = nowMs;
  for (const TrackerAction action : kDirectionOrder) {
    const std::uint16_t bit = Bit(action);
    if ((mask & bit) == 0U)
      continue;
    const std::size_t index = DirectionIndex(action);
    repeatDebt_[index] = 0U;
    nextRepeatMs_[index] = 0U;
  }
}

void InputMailbox::AccumulateRepeats(std::uint32_t nowMs) {
  for (const TrackerAction action : kDirectionOrder) {
    const std::uint16_t bit = Bit(action);
    if ((acceptedHeldMask_ & bit) == 0U)
      continue;

    const std::size_t index = DirectionIndex(action);
    const std::uint32_t deadline = nextRepeatMs_[index];
    if (!TimeReached(nowMs, deadline))
      continue;

    const std::uint32_t due =
        1U + static_cast<std::uint32_t>(nowMs - deadline) / kRepeatPeriodMs;
    SaturatingAdd(repeatDebt_[index], due);
    // Advance by the full debt, even when its retained representation
    // saturates, so draining later resumes from real time instead of bursting
    // stale repeats for several frames.
    nextRepeatMs_[index] += due * kRepeatPeriodMs;
  }
}

void InputMailbox::PublishSample(std::uint16_t physicalHeldMask,
                                 bool headphoneConnected,
                                 std::uint32_t nowMs) {
  physicalHeldMask &= kSupportedMask;
  latestPhysicalHeldMask_ = physicalHeldMask;
  latestHeadphoneConnected_ = headphoneConnected;
  latestSampleMs_ = nowMs;
  samplePending_ = true;

  if (!initialized_) {
    initialized_ = true;
    lastAcceptedTransitionMs_ = nowMs;
    AcceptPresses(physicalHeldMask, nowMs);
    return;
  }

  // Safety invariant: once a release has been observed, remember it before
  // applying the transition-kill policy to presses. A release/repress pair can
  // therefore be reconstructed even if the UI task was busy transferring LCD
  // rows during both samples.
  AcceptReleases(static_cast<std::uint16_t>(acceptedHeldMask_ &
                                            ~physicalHeldMask),
                 nowMs);

  const std::uint16_t candidatePresses = static_cast<std::uint16_t>(
      physicalHeldMask & ~acceptedHeldMask_);
  const bool killElapsed =
      static_cast<std::uint32_t>(nowMs - lastAcceptedTransitionMs_) >=
      kPressKillMs;
  if (candidatePresses != 0U && killElapsed)
    AcceptPresses(candidatePresses, nowMs);

  AccumulateRepeats(nowMs);
}

InputMailbox::Batch InputMailbox::Drain() {
  Batch batch{};
  if (!initialized_)
    return batch;

  batch.hasSample = samplePending_;
  batch.sampleTimestampMs = latestSampleMs_;
  batch.heldMask = acceptedHeldMask_;
  batch.headphoneConnected = latestHeadphoneConnected_;
  batch.headphoneChanged =
      !headphoneDelivered_ ||
      latestHeadphoneConnected_ != deliveredHeadphoneConnected_;

  // Existing logical holds must be released before any new chord is pressed.
  const std::uint16_t previouslyDeliveredMask = deliveredHeldMask_;
  const std::uint16_t releaseMask = static_cast<std::uint16_t>(
      deliveredHeldMask_ &
      (pendingReleasedMask_ |
       static_cast<std::uint16_t>(~acceptedHeldMask_)));
  for (const TrackerAction action : kAllActionOrder) {
    const std::uint16_t bit = Bit(action);
    if ((releaseMask & bit) == 0U)
      continue;
    (void)batch.Push(action, false);
    deliveredHeldMask_ &= static_cast<std::uint16_t>(~bit);
  }

  const auto pressStage = [&](const auto &order) {
    for (const TrackerAction action : order) {
      const std::uint16_t bit = Bit(action);
      if ((acceptedHeldMask_ & bit) == 0U ||
          (deliveredHeldMask_ & bit) != 0U)
        continue;

      // If this action completed press/release/press while it was not yet
      // delivered, replay the observed release before establishing the final
      // hold. This is the only three-event case and is why Batch has a small
      // margin above two events per supported action.
      if ((previouslyDeliveredMask & bit) == 0U &&
          (pendingPressedMask_ & bit) != 0U &&
          (pendingReleasedMask_ & bit) != 0U) {
        (void)batch.Push(action, true);
        (void)batch.Push(action, false);
      }
      (void)batch.Push(action, true);
      deliveredHeldMask_ |= bit;
    }
  };
  pressStage(kModifierOrder);
  pressStage(kOrdinaryOrder);

  // Preserve a complete tap that started and ended while the application task
  // was busy. These pairs cannot take part in a held chord, so they follow the
  // release/held-press priority phases as self-contained actions.
  const std::uint16_t tappedMask = static_cast<std::uint16_t>(
      pendingPressedMask_ & pendingReleasedMask_ & ~acceptedHeldMask_ &
      ~deliveredHeldMask_);
  const auto tapStage = [&](const auto &order) {
    for (const TrackerAction action : order) {
      const std::uint16_t bit = Bit(action);
      if ((tappedMask & bit) == 0U)
        continue;
      (void)batch.Push(action, true);
      (void)batch.Push(action, false);
    }
  };
  tapStage(kModifierOrder);
  tapStage(kOrdinaryOrder);

  for (const TrackerAction action : kDirectionOrder) {
    const std::size_t index = DirectionIndex(action);
    const std::uint8_t count = repeatDebt_[index];
    if (count == 0U || (acceptedHeldMask_ & Bit(action)) == 0U)
      continue;
    (void)batch.Push(action, true, count, true);
    repeatDebt_[index] = 0U;
  }

  pendingPressedMask_ = 0U;
  pendingReleasedMask_ = 0U;
  deliveredHeadphoneConnected_ = latestHeadphoneConnected_;
  headphoneDelivered_ = true;
  samplePending_ = false;
  return batch;
}

bool InputMailbox::HasPending() const {
  if (!initialized_)
    return false;
  if (samplePending_ || pendingPressedMask_ != 0U ||
      pendingReleasedMask_ != 0U ||
      acceptedHeldMask_ != deliveredHeldMask_)
    return true;
  return std::any_of(repeatDebt_.begin(), repeatDebt_.end(),
                     [](std::uint8_t value) { return value != 0U; });
}

} // namespace node::ui2
