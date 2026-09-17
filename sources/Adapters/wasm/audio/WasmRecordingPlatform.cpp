/* SPDX-License-Identifier: BSD-3-Clause */
#include "Application/Audio/RecordingPlatform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <emscripten/emscripten.h>
#include <thread>

namespace {
// Browser main is the publisher; the application pthread only reads metrics.
// Keep these values aligned with web/src/handles/recording.js.
enum State : std::uint32_t {
  Idle,
  Requesting,
  Capturing,
  Saving,
  Saved,
  Failed
};
std::array<std::atomic<std::uint32_t>, 3> snapshot{}; // state, elapsed ms, peak
std::uint32_t CurrentState() {
  return snapshot[0].load(std::memory_order_acquire);
}
} // namespace

bool IsRecordingAvailable() {
  static const bool available = MAIN_THREAD_EM_ASM_INT({
    return !!(globalThis.isSecureContext && navigator.mediaDevices?.getUserMedia &&
              globalThis.AudioContext && (globalThis.AudioWorkletNode ||
              AudioContext.prototype.createScriptProcessor) &&
              globalThis.SharedArrayBuffer);
  });
  return available;
}

bool StartRecording(const char *filename, std::uint8_t,
                    std::uint32_t duration) {
  if (!IsRecordingAvailable() || filename == nullptr || filename[0] == '\0' ||
      IsRecordingActive() || IsSavingRecording())
    return false;
  snapshot[1].store(0U, std::memory_order_release);
  snapshot[2].store(0U, std::memory_order_release);
  snapshot[0].store(Requesting, std::memory_order_release);
  // JavaScript arrow functions must not be reformatted as C++ operators.
  // clang-format off
  MAIN_THREAD_EM_ASM(
      {
        const base = $0 >>> 2;
        const publish = (state, elapsed, peak) => {
          Atomics.store(HEAPU32, base + 1, elapsed || 0);
          Atomics.store(HEAPU32, base + 2, peak || 0);
          Atomics.store(HEAPU32, base, state);
        };
        if (!Module.nullPeratorRecording) {
          publish(5);
          return;
        }
        Module.nullPeratorRecording.start(UTF8ToString($1), $2, publish);
      },
      snapshot.data(), filename,
      duration == 0U ? 30000U : std::min(duration, 30000U));
  // clang-format on
  return true;
}

void RequestStopRecording() {
  if (!IsRecordingActive())
    return;
  MAIN_THREAD_EM_ASM({ Module.nullPeratorRecording ?.stop(); });
}
void StopRecording() {
  RequestStopRecording();
  (void)WaitForRecordingStop(UINT32_MAX);
}
bool WaitForRecordingStop(std::uint32_t timeoutMs) {
  RequestStopRecording();
  const auto started = std::chrono::steady_clock::now();
  while (IsRecordingActive() || IsSavingRecording()) {
    if (timeoutMs != UINT32_MAX &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
                .count() >= timeoutMs)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}
void FinishStopRecording() {}
void Record(void *) {}
void StartMonitoring() {}
void StopMonitoring() {}
void SetInputSource(RecordSource) {}
bool IsRecordingInputSelectable() { return false; }
bool IsRecordingActive() {
  const auto state = CurrentState();
  return state == Requesting || state == Capturing;
}
bool IsMonitoringActive() { return false; }
bool IsSavingRecording() { return CurrentState() == Saving; }
std::uint8_t GetSavingProgressPercent() {
  return CurrentState() == Saved ? 100U : 0U;
}
bool DidLastRecordingCaptureAudio() { return CurrentState() == Saved; }
std::uint32_t GetRecordingElapsedMilliseconds() {
  return snapshot[1].load(std::memory_order_acquire);
}
std::uint16_t GetRecordingInputPeak() {
  return static_cast<std::uint16_t>(
      snapshot[2].load(std::memory_order_acquire));
}
