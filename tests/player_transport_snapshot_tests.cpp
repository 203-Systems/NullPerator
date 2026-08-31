/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/Player/PlayerTransportSnapshot.h"
#include "Application/Player/TransportSnapshotPublication.h"

#include "doctest/doctest.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace {

PlayerTransportSnapshot Frame(std::uint8_t token) {
  PlayerTransportSnapshot frame{};
  frame.running = token != 0U;
  frame.channelPlayingMask = token != 0U ? 0xAAU : 0x55U;
  frame.sequencerMode = token != 0U ? SM_LIVE : SM_SONG;
  frame.mode = token != 0U ? PM_LIVE : PM_SONG;
  frame.liveQueueElapsedMs = static_cast<std::uint32_t>(token) * 1234U;
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    frame.songRow[channel] = static_cast<int>(token) * 100 + channel;
    frame.note[channel] =
        static_cast<unsigned char>(static_cast<int>(token) * 40 + channel);
    frame.chain[channel] =
        static_cast<unsigned char>(static_cast<int>(token) * 50 + channel);
    frame.chainRow[channel] =
        static_cast<std::int8_t>(static_cast<int>(token) * 8 + channel);
    frame.phrase[channel] =
        static_cast<unsigned char>(static_cast<int>(token) * 60 + channel);
    frame.phraseRow[channel] =
        static_cast<std::int8_t>(static_cast<int>(token) * 8 + channel);
    frame.queueMode[channel] = token != 0U ? QM_CHAINSTART : QM_PHRASESTOP;
    frame.queueSongRow[channel] =
        static_cast<unsigned char>(static_cast<int>(token) * 70 + channel);
    frame.queueChainRow[channel] =
        static_cast<unsigned char>(static_cast<int>(token) * 8 + channel);
  }
  return frame;
}

bool SameFrame(const PlayerTransportSnapshot &left,
               const PlayerTransportSnapshot &right) {
  if (left.running != right.running ||
      left.channelPlayingMask != right.channelPlayingMask ||
      left.sequencerMode != right.sequencerMode ||
      left.reserved != right.reserved || left.mode != right.mode ||
      left.liveQueueElapsedMs != right.liveQueueElapsedMs) {
    return false;
  }
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    if (left.songRow[channel] != right.songRow[channel] ||
        left.note[channel] != right.note[channel] ||
        left.chain[channel] != right.chain[channel] ||
        left.chainRow[channel] != right.chainRow[channel] ||
        left.phrase[channel] != right.phrase[channel] ||
        left.phraseRow[channel] != right.phraseRow[channel] ||
        left.queueMode[channel] != right.queueMode[channel] ||
        left.queueSongRow[channel] != right.queueSongRow[channel] ||
        left.queueChainRow[channel] != right.queueChainRow[channel]) {
      return false;
    }
  }
  return true;
}

} // namespace

TEST_CASE("transport snapshot keeps channel activity in its size budget") {
  CHECK(sizeof(PlayerTransportSnapshot) <= 128U);
  PlayerTransportSnapshot snapshot{};
  snapshot.channelPlayingMask = 0x81U;
  CHECK(snapshot.IsChannelPlaying(0U));
  CHECK_FALSE(snapshot.IsChannelPlaying(1U));
  CHECK(snapshot.IsChannelPlaying(7U));
  CHECK_FALSE(snapshot.IsChannelPlaying(SONG_CHANNEL_COUNT));
}

TEST_CASE("transport publication never tears or drops its final frame") {
  TransportSnapshotPublication<PlayerTransportSnapshot> publication;
  const PlayerTransportSnapshot first = Frame(0U);
  const PlayerTransportSnapshot second = Frame(1U);
  const PlayerTransportSnapshot terminal = Frame(2U);
  publication.Publish(first);

  std::atomic<bool> start{false};
  std::atomic<bool> failed{false};
  constexpr int iterations = 100000;

  const auto reader = [&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const PlayerTransportSnapshot captured = publication.Capture();
      if (!SameFrame(captured, first) && !SameFrame(captured, second) &&
          !SameFrame(captured, terminal)) {
        failed.store(true, std::memory_order_relaxed);
        return;
      }
    }
  };

  std::thread firstReader(reader);
  std::thread secondReader(reader);
  std::thread writer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const PlayerTransportSnapshot &next =
          (iteration & 1) == 0 ? second : first;
      publication.Publish(next);
    }
    // A Stop/Bind/Queue edge may be the final producer action. It must remain
    // observable without relying on another audio tick to retry publication.
    publication.Publish(terminal);
  });

  start.store(true, std::memory_order_release);
  writer.join();
  firstReader.join();
  secondReader.join();
  CHECK_FALSE(failed.load(std::memory_order_relaxed));
  CHECK(SameFrame(publication.Capture(), terminal));
}
