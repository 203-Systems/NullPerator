/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PICOTRACKER_SHARED_PCM_RING_BUFFER_H
#define PICOTRACKER_SHARED_PCM_RING_BUFFER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

// Frames in the engine's interlaced signed-16-bit stereo format and the Web
// Audio-facing normalized float format.  Keep these trivially-copyable: the
// SPSC queue publishes a complete frame with one release-store.
struct StereoI16 {
  std::int16_t left = 0;
  std::int16_t right = 0;
};

struct StereoF32 {
  float left = 0.0F;
  float right = 0.0F;
};

static_assert(std::is_trivially_copyable_v<StereoI16>);
static_assert(std::is_trivially_copyable_v<StereoF32>);
static_assert(sizeof(short) == sizeof(std::int16_t) &&
                  std::numeric_limits<short>::digits ==
                      std::numeric_limits<std::int16_t>::digits,
              "PicoTracker AudioDriver samples must be signed 16-bit short");

// A fixed-capacity, single-producer/single-consumer PCM queue.  Exactly one
// thread may call Write and exactly one other thread may call Read.  Reset
// requires both endpoints to be quiescent.
//
// Write returns the number of accepted frames; any rejected frames increment
// Overruns by their count.  Read always zero-fills the requested output span,
// returns the number of real frames copied, and increments Underruns by the
// number of zero-filled frames.  Therefore both metrics are frame totals, not
// merely incident counts.
template <std::size_t CapacityFrames> class PcmRingBuffer {
  static_assert(CapacityFrames != 0,
                "a PCM ring buffer needs at least one frame");
  static_assert((CapacityFrames & (CapacityFrames - 1U)) == 0U,
                "PCM ring buffer capacity must be a power of two");

public:
  using Position = std::uint64_t;
  static constexpr std::size_t Capacity = CapacityFrames;

  [[nodiscard]] std::size_t Write(std::span<const StereoI16> input) noexcept {
    const Position write = writePosition_.load(std::memory_order_relaxed);
    const Position read = readPosition_.load(std::memory_order_acquire);
    const std::size_t used = static_cast<std::size_t>(write - read);
    const std::size_t available = CapacityFrames - used;
    const std::size_t accepted =
        input.size() < available ? input.size() : available;

    for (std::size_t index = 0; index < accepted; ++index) {
      frames_[(write + index) & (CapacityFrames - 1U)] = input[index];
    }
    if (accepted != 0U) {
      // Publishing this position makes every preceding frame write visible to
      // the reader.  The consumer only modifies readPosition_.
      writePosition_.store(write + accepted, std::memory_order_release);
    }

    const std::size_t rejected = input.size() - accepted;
    if (rejected != 0U) {
      overruns_.fetch_add(rejected, std::memory_order_relaxed);
    }
    return accepted;
  }

  // AudioDriver supplies interleaved short samples, not StereoI16 objects.
  // Read each scalar pair directly so Task 7 never needs an aliasing-unsafe
  // reinterpret_cast. An odd scalar count is malformed and is rejected as a
  // whole span; it leaves the queue and its frame counters unchanged.
  [[nodiscard]] std::size_t
  WriteInterleaved(std::span<const short> samples) noexcept {
    if ((samples.size() & 1U) != 0U) {
      return 0U;
    }

    const std::size_t inputFrames = samples.size() / 2U;
    const Position write = writePosition_.load(std::memory_order_relaxed);
    const Position read = readPosition_.load(std::memory_order_acquire);
    const std::size_t used = static_cast<std::size_t>(write - read);
    const std::size_t available = CapacityFrames - used;
    const std::size_t accepted =
        inputFrames < available ? inputFrames : available;

    for (std::size_t index = 0; index < accepted; ++index) {
      const std::size_t sampleIndex = index * 2U;
      frames_[(write + index) & (CapacityFrames - 1U)] = {
          static_cast<std::int16_t>(samples[sampleIndex]),
          static_cast<std::int16_t>(samples[sampleIndex + 1U])};
    }
    if (accepted != 0U) {
      writePosition_.store(write + accepted, std::memory_order_release);
    }

    const std::size_t rejected = inputFrames - accepted;
    if (rejected != 0U) {
      overruns_.fetch_add(rejected, std::memory_order_relaxed);
    }
    return accepted;
  }

  [[nodiscard]] std::size_t Read(std::span<StereoF32> output) noexcept {
    const Position read = readPosition_.load(std::memory_order_relaxed);
    const Position write = writePosition_.load(std::memory_order_acquire);
    const std::size_t available = static_cast<std::size_t>(write - read);
    const std::size_t copied =
        output.size() < available ? output.size() : available;

    for (std::size_t index = 0; index < copied; ++index) {
      const StereoI16 frame = frames_[(read + index) & (CapacityFrames - 1U)];
      // 32768 maps INT16_MIN exactly to -1 and keeps INT16_MAX strictly below
      // +1, without negating the minimum signed value.
      output[index] = {static_cast<float>(frame.left) / 32768.0F,
                       static_cast<float>(frame.right) / 32768.0F};
    }
    for (std::size_t index = copied; index < output.size(); ++index) {
      output[index] = {};
    }
    if (copied != 0U) {
      // Releasing the new read position permits the producer to reuse these
      // slots only after all reads above have completed.
      readPosition_.store(read + copied, std::memory_order_release);
    }

    const std::size_t missing = output.size() - copied;
    if (missing != 0U) {
      underruns_.fetch_add(missing, std::memory_order_relaxed);
    }
    return copied;
  }

  [[nodiscard]] std::size_t FillFrames() const noexcept {
    const Position write = writePosition_.load(std::memory_order_acquire);
    const Position read = readPosition_.load(std::memory_order_acquire);
    const std::size_t fill = static_cast<std::size_t>(write - read);
    // A monitoring thread can observe a pre-producer write position followed
    // by a post-producer/consumer read position.  Do not expose that crossed
    // snapshot as a huge unsigned value; the SPSC endpoints themselves cannot
    // observe it because each owns one of the two positions.
    return fill <= CapacityFrames ? fill : 0U;
  }

  [[nodiscard]] std::uint64_t Underruns() const noexcept {
    return underruns_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t Overruns() const noexcept {
    return overruns_.load(std::memory_order_relaxed);
  }

  // This is deliberately not a concurrent operation.  It is for startup and
  // deterministic tests, when producer and consumer have both stopped.
  void Reset() noexcept {
    writePosition_.store(0U, std::memory_order_relaxed);
    readPosition_.store(0U, std::memory_order_relaxed);
    underruns_.store(0U, std::memory_order_relaxed);
    overruns_.store(0U, std::memory_order_relaxed);
  }

#ifdef HOST_TEST
  // Host-test seam for modular-position coverage. It is unavailable in the
  // browser target and must only be called while both SPSC endpoints are idle.
  void SeedPositionsForTest(Position read, Position write) noexcept {
    readPosition_.store(read, std::memory_order_relaxed);
    writePosition_.store(write, std::memory_order_relaxed);
  }
#endif

private:
  alignas(64) std::array<StereoI16, CapacityFrames> frames_{};
  alignas(64) std::atomic<Position> writePosition_{0U};
  alignas(64) std::atomic<Position> readPosition_{0U};
  std::atomic<std::uint64_t> underruns_{0U};
  std::atomic<std::uint64_t> overruns_{0U};
};

#endif
