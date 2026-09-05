/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include <atomic>
#include <cstdint>

struct NodeAudioMetrics {
  std::uint32_t blocks, frames, maxRenderUs, maxLoadPermille;
  std::uint32_t deadlineMisses, producerStarvations, writeErrors;
};

// Each counter has one writer (render task or I2S task). UI reads cumulative
// diagnostic counters without taking the mixer lock or logging from audio.
class NodeAudioTelemetry {
public:
  void RecordRender(std::uint32_t micros, std::uint32_t frames) {
    if (frames == 0)
      return;
    const std::uint64_t timeAtRate = std::uint64_t(micros) * 44100U;
    const std::uint64_t budget = std::uint64_t(frames) * 1000000U;
    const std::uint32_t load = static_cast<std::uint32_t>(
        (timeAtRate + std::uint64_t(frames) * 1000U - 1U) /
        (std::uint64_t(frames) * 1000U));
    frames_.fetch_add(frames, std::memory_order_relaxed);
    blocks_.fetch_add(1, std::memory_order_relaxed);
    if (micros > maxRenderUs_.load(std::memory_order_relaxed))
      maxRenderUs_.store(micros, std::memory_order_relaxed);
    if (load > maxLoadPermille_.load(std::memory_order_relaxed))
      maxLoadPermille_.store(load, std::memory_order_relaxed);
    if (timeAtRate > budget)
      deadlineMisses_.fetch_add(1, std::memory_order_relaxed);
  }
  void RecordStarvation() {
    producerStarvations_.fetch_add(1, std::memory_order_relaxed);
  }
  void RecordWriteError() {
    writeErrors_.fetch_add(1, std::memory_order_relaxed);
  }
  NodeAudioMetrics Read() const {
    return {blocks_.load(std::memory_order_relaxed),
            frames_.load(std::memory_order_relaxed),
            maxRenderUs_.load(std::memory_order_relaxed),
            maxLoadPermille_.load(std::memory_order_relaxed),
            deadlineMisses_.load(std::memory_order_relaxed),
            producerStarvations_.load(std::memory_order_relaxed),
            writeErrors_.load(std::memory_order_relaxed)};
  }

private:
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  std::atomic<std::uint32_t> blocks_{0}, frames_{0}, maxRenderUs_{0};
  std::atomic<std::uint32_t> maxLoadPermille_{0}, deadlineMisses_{0};
  std::atomic<std::uint32_t> producerStarvations_{0}, writeErrors_{0};
};

NodeAudioTelemetry &GetNodeAudioTelemetry();
