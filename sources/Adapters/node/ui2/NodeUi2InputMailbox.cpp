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

constexpr std::size_t OrdinaryIndex(TrackerAction action) {
  for (std::size_t index = 0U; index < kOrdinaryOrder.size(); ++index) {
    if (kOrdinaryOrder[index] == action)
      return index;
  }
  return kOrdinaryOrder.size();
}

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

  const std::uint16_t acceptedWithPresses =
      static_cast<std::uint16_t>(acceptedHeldMask_ | mask);
  for (const TrackerAction action : kOrdinaryOrder) {
    if ((mask & Bit(action)) == 0U)
      continue;
    pendingModifierContext_[OrdinaryIndex(action)] =
        static_cast<std::uint16_t>(acceptedWithPresses & kModifierMask);
  }
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

  const std::uint16_t tappedMask = static_cast<std::uint16_t>(
      pendingPressedMask_ & pendingReleasedMask_ & ~acceptedHeldMask_ &
      kSupportedMask);

  // A common short chord (for example SHIFT+LEFT navigation) can be pressed
  // and released while the UI task is transferring LCD rows. When exactly one
  // ordinary action completed, its sampled modifier context is unambiguous;
  // keep those modifiers down around the action tap. More complex coalesced
  // histories retain the conservative legacy ordering because bit masks alone
  // cannot recover their chronology without guessing.
  TrackerAction completedChordAction = TrackerAction::Count;
  std::uint16_t completedChordModifiers = 0U;
  std::uint8_t tappedOrdinaryCount = 0U;
  for (const TrackerAction action : kOrdinaryOrder) {
    if ((tappedMask & Bit(action)) == 0U)
      continue;
    ++tappedOrdinaryCount;
    completedChordAction = action;
    completedChordModifiers = pendingModifierContext_[OrdinaryIndex(action)];
  }
  if (tappedOrdinaryCount != 1U || completedChordModifiers == 0U ||
      (acceptedHeldMask_ & kModifierMask & ~completedChordModifiers) != 0U) {
    completedChordAction = TrackerAction::Count;
    completedChordModifiers = 0U;
  }

  // Existing logical holds must be released before any new chord is pressed.
  const std::uint16_t previouslyDeliveredMask = deliveredHeldMask_;
  const std::uint16_t releaseMask = static_cast<std::uint16_t>(
      deliveredHeldMask_ &
      (pendingReleasedMask_ |
       static_cast<std::uint16_t>(~acceptedHeldMask_)));
  // A key that was already delivered can complete another physical cycle
  // before this drain. Its first release is a real application boundary:
  // EDIT release commits/stops audition and PLAY release transfers ownership.
  // Do not keep a recycled modifier logically held merely because its new
  // press supplied the context for the queued chord.
  const std::uint16_t recycledDeliveredMask = static_cast<std::uint16_t>(
      previouslyDeliveredMask & pendingReleasedMask_ & pendingPressedMask_);
  const std::uint16_t deferredChordReleases =
      static_cast<std::uint16_t>(releaseMask & completedChordModifiers &
                                 ~recycledDeliveredMask);
  const std::uint16_t immediateReleaseMask =
      static_cast<std::uint16_t>(releaseMask & ~deferredChordReleases);
  for (const TrackerAction action : kAllActionOrder) {
    const std::uint16_t bit = Bit(action);
    if ((immediateReleaseMask & bit) == 0U)
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

  // Preserve complete taps that started and ended while the application task
  // was busy. The single-action chord case below retains its sampled modifier
  // context; all remaining pairs follow the conservative self-contained order.
  const auto tapStage = [&](const auto &order) {
    for (const TrackerAction action : order) {
      const std::uint16_t bit = Bit(action);
      if ((tappedMask & bit) == 0U || action == completedChordAction ||
          (completedChordModifiers & bit) != 0U)
        continue;
      (void)batch.Push(action, true);
      (void)batch.Push(action, false);
    }
  };
  tapStage(kModifierOrder);

  if (completedChordAction != TrackerAction::Count) {
    for (const TrackerAction modifier : kModifierOrder) {
      const std::uint16_t bit = Bit(modifier);
      if ((completedChordModifiers & bit) == 0U ||
          (deliveredHeldMask_ & bit) != 0U)
        continue;
      (void)batch.Push(modifier, true);
      deliveredHeldMask_ |= bit;
    }
    (void)batch.Push(completedChordAction, true);
    (void)batch.Push(completedChordAction, false);
    for (auto iterator = kModifierOrder.rbegin();
         iterator != kModifierOrder.rend(); ++iterator) {
      const std::uint16_t bit = Bit(*iterator);
      if ((completedChordModifiers & bit) == 0U ||
          (acceptedHeldMask_ & bit) != 0U)
        continue;
      (void)batch.Push(*iterator, false);
      deliveredHeldMask_ &= static_cast<std::uint16_t>(~bit);
    }
  }
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
  pendingModifierContext_.fill(0U);
  deliveredHeadphoneConnected_ = latestHeadphoneConnected_;
  headphoneDelivered_ = true;
  samplePending_ = false;
  return batch;
}

} // namespace node::ui2
