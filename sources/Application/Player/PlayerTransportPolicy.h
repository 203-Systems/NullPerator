/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstdint>

struct SongTrackRange final {
  unsigned int first = 0;
  unsigned int last = 0;
};

// Song transport is also reachable from MIDI and adapter boundaries, so clamp
// before indexing the fixed eight-channel queue. Reversed endpoints retain the
// inclusive selection semantics used by the Song view.
template <unsigned int ChannelCount>
[[nodiscard]] constexpr SongTrackRange
NormalizeSongTrackRange(unsigned int first, unsigned int last) noexcept {
  static_assert(ChannelCount > 0);
  constexpr unsigned int upperBound = ChannelCount - 1U;
  first = first > upperBound ? upperBound : first;
  last = last > upperBound ? upperBound : last;
  return first <= last ? SongTrackRange{first, last}
                       : SongTrackRange{last, first};
}

// Resolve every immediate queue before normal step advancement. The returned
// bit mask records channels whose new position was accepted, allowing the
// caller to avoid advancing those positions twice in the same audio tick.
template <unsigned int ChannelCount, typename IsImmediate, typename Trigger>
[[nodiscard]] constexpr std::uint32_t
TriggerAllImmediateChannels(IsImmediate isImmediate, Trigger trigger) {
  static_assert(ChannelCount > 0 && ChannelCount <= 32);
  std::uint32_t triggered = 0;
  for (unsigned int channel = 0; channel < ChannelCount; ++channel) {
    if (isImmediate(channel) && trigger(channel)) {
      triggered |= std::uint32_t{1} << channel;
    }
  }
  return triggered;
}

// A channel can publish the stop-at-end edge while the shared step traversal
// is in progress. Stop immediately so later channels and the remainder of the
// same audio tick cannot overwrite the final stopped transport snapshot.
template <unsigned int ChannelCount, typename Advance>
[[nodiscard]] constexpr bool
AdvanceTransportChannelsUntilStopped(Advance advance) {
  static_assert(ChannelCount > 0);
  for (unsigned int channel = 0; channel < ChannelCount; ++channel) {
    if (!advance(channel)) {
      return false;
    }
  }
  return true;
}
