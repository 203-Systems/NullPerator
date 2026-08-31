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
  static void Configure(std::uint32_t targetFillFrames,
                        std::uint32_t outputGainQ16) noexcept;

  // Called only by asynchronous Emscripten setup callbacks or the worklet.
  static void MarkRunning() noexcept;
  static void MarkFailed(const char *message) noexcept;
  static void OnContextResumed(int state) noexcept;
  static void OnWorkletThreadStarted(bool success) noexcept;
  static void OnProcessorCreated(bool success) noexcept;

private:
  static constexpr std::uint32_t ConfiguredTargetBits = 14U;
  static constexpr std::uint32_t ConfiguredTargetMask =
      (1U << ConfiguredTargetBits) - 1U;

  static void SetState(WasmAudioState state) noexcept;
  static void SetError(const char *message) noexcept;
  static void AdvanceSetupPhase(std::uint32_t phase) noexcept;
  static void PumpBrowserMainSetup(void *) noexcept;
  static void BrowserMainTeardown() noexcept;
  static void ApplyConfiguration(WasmAudioDriver &driver) noexcept;
  bool initialized_ = false;
  std::atomic<std::uint32_t> volume_{100U};

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
  // Target fill (14 bits) and browser-host gain (17 bits) fit in one atomic
  // word, so pre-main and live browser configuration cannot publish a mixed
  // pair to the application pthread.
  static std::atomic<std::uint32_t> configuredAudio_;
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "Wasm audio state requires lock-free 32-bit atomics");
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
