/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Session/PlayMode.h"

// Fixed-size, allocation-free decisions made before Player touches the mixer.
// Keeping this pure lets host tests cover transport context without requiring
// an audio driver while compiling to a handful of integer operations on ESP32.
struct PlayerStartPlan final {
  PlayMode mode = PM_SONG;
  bool resumeLastSongPosition = false;
  bool stopAtEnd = false;
  int contextChannel = 0;
  int contextChainPosition = 0;
};

[[nodiscard]] constexpr int ClampPlayerStartIndex(int value,
                                                   int upperBound) noexcept {
  return value < 0 ? 0 : (value > upperBound ? upperBound : value);
}

template <int ChannelCount, int ChainPositionCount>
[[nodiscard]] constexpr PlayerStartPlan ResolvePlayerStartPlan(
    PlayMode mode, bool resumeLastSongPosition, bool stopAtEnd,
    int requestedChannel, int requestedChainPosition, int fallbackChannel,
    int fallbackChainPosition) noexcept {
  static_assert(ChannelCount > 0);
  static_assert(ChainPositionCount > 0);
  const int channel =
      requestedChannel < 0 ? fallbackChannel : requestedChannel;
  const int chainPosition = requestedChainPosition < 0
                                ? fallbackChainPosition
                                : requestedChainPosition;
  return {
      resumeLastSongPosition ? PM_SONG : mode,
      resumeLastSongPosition,
      stopAtEnd,
      ClampPlayerStartIndex(channel, ChannelCount - 1),
      ClampPlayerStartIndex(chainPosition, ChainPositionCount - 1),
  };
}

template <int ChannelCount, int ChainPositionCount>
[[nodiscard]] constexpr PlayerStartPlan ResolveContextStartPlan(
    PlayMode mode, bool resumeLastSongPosition, bool stopAtEnd,
    unsigned int channel, unsigned char chainPosition) noexcept {
  const int boundedChannel =
      channel < static_cast<unsigned int>(ChannelCount)
          ? static_cast<int>(channel)
          : ChannelCount - 1;
  const int boundedChainPosition =
      chainPosition < static_cast<unsigned int>(ChainPositionCount)
          ? static_cast<int>(chainPosition)
          : ChainPositionCount - 1;
  return ResolvePlayerStartPlan<ChannelCount, ChainPositionCount>(
      mode, resumeLastSongPosition, stopAtEnd, boundedChannel,
      boundedChainPosition, 0, 0);
}
