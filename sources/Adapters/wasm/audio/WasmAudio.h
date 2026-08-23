/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "WasmAudioState.h"
#include "Services/Audio/Audio.h"

#include <atomic>
#include <array>
#include <cstdint>

class WasmAudioDriver;

class WasmAudio final : public Audio {
public:
  explicit WasmAudio(AudioSettings &settings);

  void Init() override;
  void Close() override;
  int GetMixerVolume() override;
  void SetMixerVolume(int volume) override;

  // Must run from the browser's C main before the application pthread starts.
  // It creates a suspended context and initializes the WASM worklet scope;
  // neither processor nor graph is started until an explicit user unlock.
  static void BootstrapBrowserMain() noexcept;
  // Published before PROXY_TO_PTHREAD enters C main when the native
  // JavaScript probe proves AudioWorklet module loading is unavailable.
  // This path must not invoke WebAudio or Emscripten worklet APIs.
  static void MarkUnavailable() noexcept;
  static bool Unlock() noexcept;
  // Schedules node/context destruction on the browser-main Emscripten
  // runtime when called by the application pthread.
  static void StopBrowserAudio() noexcept;
  [[nodiscard]] static bool BrowserTeardownComplete() noexcept;
  [[nodiscard]] static WasmAudioState State() noexcept;
  [[nodiscard]] static const char *LastError() noexcept;
  [[nodiscard]] static const WasmAudioMetrics *CopyMetrics() noexcept;
  // Shared, lock-free ABI snapshots read by JavaScript after C main has
  // moved to the application pthread. Their addresses are cached while the
  // browser main owns onRuntimeInitialized; callers must use Atomics.load.
  [[nodiscard]] static const std::uint32_t *MetricsSnapshotAddress() noexcept;
  [[nodiscard]] static const std::uint32_t *ErrorSnapshotAddress() noexcept;
  // Application rAF is the sole snapshot writer. Browser-main callbacks and
  // the AudioWorklet only update their source atomics; they must never begin
  // a competing seqlock publication.
  static void PublishSnapshot() noexcept;

  // Called only by asynchronous Emscripten setup callbacks or the worklet.
  static void MarkRunning() noexcept;
  static void MarkFailed(const char *message) noexcept;
  static void OnContextResumed(int state) noexcept;
  static void OnWorkletThreadStarted(bool success) noexcept;
  static void OnProcessorCreated(bool success) noexcept;

private:
  static void SetState(WasmAudioState state) noexcept;
  static void SetError(const char *message) noexcept;
  static void PumpBrowserMainSetup(void *) noexcept;
  static void BrowserMainTeardown() noexcept;
  bool initialized_ = false;
  int volume_ = 100;

  static std::atomic<WasmAudioState> state_;
  static std::atomic<bool> unlockStarted_;
  static std::atomic<bool> browserStopped_;
  static std::atomic<bool> teardownRequested_;
  static std::atomic<bool> teardownComplete_;
  static std::atomic<bool> scopeReady_;
  static std::atomic<bool> driverReady_;
  static std::atomic<bool> processorRequested_;
  static std::atomic<std::uint32_t> setupWatchdogTicks_;
  static std::atomic<std::uint32_t> processorRequestedTick_;
  static std::atomic<int> context_;
  static std::atomic<int> workletNode_;
  static std::atomic<std::uint32_t> setupPhase_;
  static std::atomic<std::uint32_t> unlockOnBrowserMainThread_;
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "Wasm audio snapshots require lock-free 32-bit atomics");
  static constexpr std::size_t MetricsWords = sizeof(WasmAudioMetrics) / sizeof(std::uint32_t);
  // Sequence followed by the fixed metrics payload. Writers publish odd,
  // write every payload word, then release an even sequence. Browser readers
  // retry unless both sequence reads match and are even.
  static constexpr std::size_t MetricsSnapshotWords = MetricsWords + 1U;
  static constexpr std::size_t ErrorBytes = 160U;
  static constexpr std::size_t ErrorWords = ErrorBytes / sizeof(std::uint32_t);
  static std::array<std::atomic<std::uint32_t>, MetricsSnapshotWords> metricsSnapshot_;
  static std::array<std::atomic<std::uint32_t>, ErrorWords> errorSnapshot_;
  static thread_local std::array<char, ErrorBytes> errorCopy_;
};
