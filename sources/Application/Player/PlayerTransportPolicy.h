/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

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
