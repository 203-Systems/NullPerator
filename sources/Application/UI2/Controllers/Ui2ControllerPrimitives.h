/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ui2 {

// Small input latch shared by non-grid UI2 controllers. Repeated pressed
// events intentionally remain observable so a platform key-repeat source can
// drive list and value movement without timers or callbacks in the controller.
class Ui2ControllerInputState {
public:
  constexpr bool Update(TrackerAction action, bool pressed) {
    if (action >= TrackerAction::Count)
      return false;
    const std::uint16_t bit = TrackerActionBit(action);
    if (pressed)
      mask_ = static_cast<std::uint16_t>(mask_ | bit);
    else
      mask_ = static_cast<std::uint16_t>(mask_ & ~bit);
    return true;
  }

  [[nodiscard]] constexpr bool Held(TrackerAction action) const {
    return action < TrackerAction::Count &&
           (mask_ & TrackerActionBit(action)) != 0U;
  }

  [[nodiscard]] constexpr bool AnyModifier() const {
    constexpr std::uint16_t modifiers =
        TrackerActionBit(TrackerAction::Shift) |
        TrackerActionBit(TrackerAction::Option) |
        TrackerActionBit(TrackerAction::Edit);
    return (mask_ & modifiers) != 0U;
  }

  [[nodiscard]] constexpr std::uint16_t Mask() const { return mask_; }

private:
  std::uint16_t mask_ = 0;
};

// Platform key-repeat sources resend key-down while the physical key remains
// held. Direction repeats are intentional for navigation and value editing;
// action/modifier repeats must remain a single edge so SAVE, IMPORT, PLAY,
// and modal openers cannot fire again before their matching key-up.
[[nodiscard]] constexpr bool
Ui2AcceptInputEvent(TrackerAction action, bool pressed,
                    std::uint16_t heldBefore) {
  if (action >= TrackerAction::Count)
    return false;
  if (!pressed || (heldBefore & TrackerActionBit(action)) == 0U)
    return true;
  return action == TrackerAction::Left || action == TrackerAction::Right ||
         action == TrackerAction::Up || action == TrackerAction::Down;
}

// A newly opened controller must not consume the press (or held companion
// inputs) that opened it. The gate stays closed until that trigger is
// released, without timers, allocation, or knowledge of platform repeat
// policy.
class Ui2InputReleaseGate {
public:
  constexpr void BlockUntilRelease(TrackerAction trigger) {
    blockedMask_ = trigger < TrackerAction::Count
                       ? TrackerActionBit(trigger)
                       : 0U;
  }

  [[nodiscard]] constexpr bool Update(TrackerAction action, bool pressed) {
    if (action >= TrackerAction::Count)
      return false;
    if (!pressed)
      blockedMask_ = static_cast<std::uint16_t>(
          blockedMask_ & ~TrackerActionBit(action));
    return blockedMask_ == 0U;
  }

  constexpr void Reset() { blockedMask_ = 0U; }

private:
  std::uint16_t blockedMask_ = 0U;
};

// Clamp a browser/list cursor after a signed movement. The fixed-capacity UI2
// browsers share this primitive for ordinary single-row movement and M8-style
// OPTION+UP/DOWN eight-row jumps.
[[nodiscard]] constexpr std::uint16_t
Ui2MoveListIndex(std::uint16_t current, std::uint16_t count,
                 std::int16_t delta) {
  if (count == 0U)
    return 0U;
  const std::int32_t maximum = static_cast<std::int32_t>(count) - 1;
  const std::int32_t moved = static_cast<std::int32_t>(current) + delta;
  return static_cast<std::uint16_t>(
      moved < 0 ? 0 : moved > maximum ? maximum : moved);
}

// A selector owns indices only. Display strings remain static/model-owned, so
// the controller is deterministic and has no lifetime or allocation concerns.
struct Ui2SelectorState {
  std::uint16_t count = 0;
  std::uint16_t current = 0;
  bool wrap = false;

  constexpr Ui2SelectorState() = default;
  constexpr Ui2SelectorState(std::uint16_t optionCount,
                             std::uint16_t selected, bool shouldWrap)
      : count(optionCount), current(optionCount == 0U
                                        ? 0U
                                        : selected < optionCount
                                              ? selected
                                              : optionCount - 1U),
        wrap(shouldWrap) {}

  [[nodiscard]] constexpr bool Valid() const { return count > 0U; }

  constexpr bool Move(std::int8_t delta) {
    if (count <= 1U || delta == 0)
      return false;
    const std::uint16_t previous = current;
    if (delta < 0) {
      if (current > 0U)
        --current;
      else if (wrap)
        current = static_cast<std::uint16_t>(count - 1U);
    } else {
      if (current + 1U < count)
        ++current;
      else if (wrap)
        current = 0U;
    }
    return current != previous;
  }
};

// Cursor indices are logical page rows. firstVisibleOrdinal is an ordinal in
// the enabled-row sequence, which means conditional rows can be skipped
// without allocating or rebuilding a second list.
template <std::size_t Capacity> class Ui2FixedListCursor {
public:
  static_assert(Capacity > 0U && Capacity <= 32U);

  static constexpr std::uint32_t AllEnabledMask =
      Capacity == 32U ? 0xFFFFFFFFU
                      : (std::uint32_t{1} << Capacity) - 1U;

  constexpr Ui2FixedListCursor(
      std::uint8_t selected = 0,
      std::uint32_t enabledMask = AllEnabledMask,
      std::uint8_t viewportRows = static_cast<std::uint8_t>(Capacity))
      : enabledMask_(enabledMask & AllEnabledMask),
        viewportRows_(SanitizeViewport(viewportRows)) {
    SanitizeSelection(selected);
    EnsureVisible();
  }

  [[nodiscard]] constexpr std::uint8_t Selected() const {
    return selected_;
  }
  [[nodiscard]] constexpr std::uint8_t SelectedOrdinal() const {
    return OrdinalOf(selected_);
  }
  [[nodiscard]] constexpr std::uint8_t FirstVisibleOrdinal() const {
    return firstVisibleOrdinal_;
  }
  [[nodiscard]] constexpr std::uint8_t EnabledCount() const {
    std::uint8_t count = 0;
    for (std::uint8_t index = 0; index < Capacity; ++index) {
      if (IsEnabled(index))
        ++count;
    }
    return count;
  }
  [[nodiscard]] constexpr bool IsEnabled(std::uint8_t index) const {
    return index < Capacity &&
           (enabledMask_ & (std::uint32_t{1} << index)) != 0U;
  }
  [[nodiscard]] constexpr std::uint32_t EnabledMask() const {
    return enabledMask_;
  }

  constexpr void SetEnabledMask(std::uint32_t enabledMask) {
    enabledMask_ = enabledMask & AllEnabledMask;
    SanitizeSelection(selected_);
    ClampFirstVisible();
    EnsureVisible();
  }

  constexpr bool MovePrevious(bool wrap = false) {
    const std::uint8_t count = EnabledCount();
    if (count <= 1U)
      return false;
    const std::uint8_t current = SelectedOrdinal();
    if (current == 0U && !wrap)
      return false;
    const std::uint8_t next =
        current == 0U ? static_cast<std::uint8_t>(count - 1U)
                      : static_cast<std::uint8_t>(current - 1U);
    selected_ = IndexAtOrdinal(next);
    EnsureVisible();
    return true;
  }

  constexpr bool MoveNext(bool wrap = false) {
    const std::uint8_t count = EnabledCount();
    if (count <= 1U)
      return false;
    const std::uint8_t current = SelectedOrdinal();
    if (current + 1U >= count && !wrap)
      return false;
    const std::uint8_t next =
        current + 1U >= count ? 0U
                              : static_cast<std::uint8_t>(current + 1U);
    selected_ = IndexAtOrdinal(next);
    EnsureVisible();
    return true;
  }

private:
  [[nodiscard]] static constexpr std::uint8_t
  SanitizeViewport(std::uint8_t rows) {
    if (rows == 0U)
      return 1U;
    return rows > Capacity ? static_cast<std::uint8_t>(Capacity) : rows;
  }

  constexpr void SanitizeSelection(std::uint8_t requested) {
    if (EnabledCount() == 0U) {
      selected_ = 0U;
      firstVisibleOrdinal_ = 0U;
      return;
    }
    selected_ = IsEnabled(requested) ? requested : IndexAtOrdinal(0U);
  }

  [[nodiscard]] constexpr std::uint8_t
  OrdinalOf(std::uint8_t index) const {
    std::uint8_t ordinal = 0;
    for (std::uint8_t candidate = 0; candidate < Capacity; ++candidate) {
      if (!IsEnabled(candidate))
        continue;
      if (candidate == index)
        return ordinal;
      ++ordinal;
    }
    return 0U;
  }

  [[nodiscard]] constexpr std::uint8_t
  IndexAtOrdinal(std::uint8_t ordinal) const {
    std::uint8_t current = 0;
    for (std::uint8_t index = 0; index < Capacity; ++index) {
      if (!IsEnabled(index))
        continue;
      if (current == ordinal)
        return index;
      ++current;
    }
    return 0U;
  }

  constexpr void ClampFirstVisible() {
    const std::uint8_t count = EnabledCount();
    const std::uint8_t maximum =
        count > viewportRows_
            ? static_cast<std::uint8_t>(count - viewportRows_)
            : 0U;
    if (firstVisibleOrdinal_ > maximum)
      firstVisibleOrdinal_ = maximum;
  }

  constexpr void EnsureVisible() {
    if (EnabledCount() == 0U) {
      firstVisibleOrdinal_ = 0U;
      return;
    }
    const std::uint8_t selected = SelectedOrdinal();
    if (selected < firstVisibleOrdinal_) {
      firstVisibleOrdinal_ = selected;
    } else if (selected >= firstVisibleOrdinal_ + viewportRows_) {
      firstVisibleOrdinal_ =
          static_cast<std::uint8_t>(selected - viewportRows_ + 1U);
    }
    ClampFirstVisible();
  }

  std::uint32_t enabledMask_ = AllEnabledMask;
  std::uint8_t selected_ = 0;
  std::uint8_t firstVisibleOrdinal_ = 0;
  std::uint8_t viewportRows_ = static_cast<std::uint8_t>(Capacity);
};

static_assert(std::is_trivially_copyable_v<Ui2ControllerInputState>);
static_assert(std::is_trivially_copyable_v<Ui2InputReleaseGate>);
static_assert(std::is_trivially_copyable_v<Ui2SelectorState>);
static_assert(sizeof(Ui2ControllerInputState) == 2U);
static_assert(sizeof(Ui2InputReleaseGate) == 2U);
static_assert(Ui2AcceptInputEvent(TrackerAction::Up, true,
                                 TrackerActionBit(TrackerAction::Up)));
static_assert(!Ui2AcceptInputEvent(TrackerAction::Edit, true,
                                  TrackerActionBit(TrackerAction::Edit)));
static_assert(Ui2MoveListIndex(3U, 12U, -8) == 0U);
static_assert(Ui2MoveListIndex(3U, 12U, 8) == 11U);
static_assert(Ui2MoveListIndex(0U, 0U, 8) == 0U);

} // namespace ui2
