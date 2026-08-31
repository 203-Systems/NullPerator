/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmAudio.h"

#include "Adapters/wasm/tracing/WasmProfiler.h"

#include "AudioWorklet.h"
#include "WasmAudioBridge.h"
#include "WasmAudioDriver.h"
#include "Adapters/wasm/platform/WasmApplicationSnapshot.h"
#include "Adapters/wasm/platform/WasmBrowserSnapshots.h"
#include "Application/Audio/RecordingPlatform.h"
#include "Application/Model/Config.h"
#include "Services/Audio/AudioOutDriver.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/eventloop.h>
#include <emscripten/webaudio.h>
#include <emscripten/threading.h>
#endif

#include <algorithm>
#include <cstring>
#include <new>

std::atomic<WasmAudioState> WasmAudio::state_{WasmAudioState::Locked};
std::atomic<bool> WasmAudio::unlockStarted_{false};
std::atomic<bool> WasmAudio::browserStopped_{false};
std::atomic<bool> WasmAudio::teardownRequested_{false};
std::atomic<bool> WasmAudio::teardownComplete_{true};
std::atomic<bool> WasmAudio::scopeReady_{false};
std::atomic<bool> WasmAudio::driverReady_{false};
std::atomic<bool> WasmAudio::processorRequested_{false};
std::atomic<std::uint32_t> WasmAudio::setupWatchdogTicks_{0U};
std::atomic<std::uint32_t> WasmAudio::processorRequestedTick_{0U};
std::atomic<int> WasmAudio::context_{0};
std::atomic<int> WasmAudio::workletNode_{0};
std::atomic<std::uint32_t> WasmAudio::setupPhase_{0U};
std::atomic<std::uint32_t> WasmAudio::unlockOnBrowserMainThread_{0U};
std::atomic<std::uint32_t> WasmAudio::configuredAudio_{
    WasmAudioDriver::TargetFillFrames |
    (WasmAudioDriver::UnityGainQ16 << WasmAudio::ConfiguredTargetBits)};
std::array<std::atomic<std::uint32_t>, WasmAudio::MetricsSnapshotWords>
    WasmAudio::metricsSnapshot_{};
std::array<std::atomic<std::uint32_t>, WasmAudio::ErrorWords>
    WasmAudio::errorSnapshot_{};
thread_local std::array<char, WasmAudio::ErrorBytes> WasmAudio::errorCopy_{};

namespace {
alignas(WasmAudioDriver) unsigned char driverStorage[sizeof(WasmAudioDriver)];
alignas(AudioOutDriver) unsigned char outputStorage[sizeof(AudioOutDriver)];
alignas(WasmAudioWorkletRenderer)
    unsigned char rendererStorage[sizeof(WasmAudioWorkletRenderer)];
WasmAudioWorkletRenderer *renderer = nullptr;
thread_local WasmAudioMetrics metricsCopy{};
WasmBrowserSnapshots browserSnapshots{};

#ifdef __EMSCRIPTEN__
alignas(16) unsigned char workletStack[64U * 1024U];

void ContextResumed(EMSCRIPTEN_WEBAUDIO_T, AUDIO_CONTEXT_STATE state,
                    void *) {
  WasmAudio::OnContextResumed(state);
}

void WorkletThreadStarted(EMSCRIPTEN_WEBAUDIO_T, bool success, void *) {
  WasmAudio::OnWorkletThreadStarted(success);
}

void ProcessorCreated(EMSCRIPTEN_WEBAUDIO_T, bool success, void *) {
  WasmAudio::OnProcessorCreated(success);
}
#endif
} // namespace

__attribute__((constructor)) void InitializeBrowserSnapshots() {
  browserSnapshots.frameData = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(Wasm_FrameSnapshotAddress()));
  browserSnapshots.frameSequence = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(Wasm_FrameSequenceAddress()));
  browserSnapshots.audioMetrics = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(WasmAudio::MetricsSnapshotAddress()));
  browserSnapshots.audioError = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(WasmAudio::ErrorSnapshotAddress()));
  // Rendering the deterministic resampler oracle is intentionally not part
  // of module construction: Emscripten's factory is not re-entrant while its
  // pthread runtime is wiring up. The browser-facing runtime treats it as an
  // optional offline diagnostic; host coverage exercises it directly.
  browserSnapshots.audioOracles = 0U;
  browserSnapshots.application = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(Wasm_ApplicationSnapshotAddress()));
}

WasmAudio::WasmAudio(AudioSettings &settings) : Audio(settings) {
  settings_ = settings;
}

void WasmAudio::Init() {
  if (initialized_) {
    return;
  }
  int mixerVolume = 40;
  if (Config *config = Config::GetInstance()) {
    if (Variable *configuredVolume =
            config->FindVariable(FourCC::VarOutputVolume)) {
      mixerVolume = configuredVolume->GetInt();
    }
  }
  mixerVolume = std::clamp(mixerVolume, 0, 100);
  volume_.store(static_cast<std::uint32_t>(mixerVolume),
                std::memory_order_release);

  AudioSettings settings{};
  settings.audioAPI_ = "wasm-audio-worklet";
  settings.audioDevice_ = "browser-default";
  settings.bufferSize_ = 1024;
  settings.preBufferCount_ = 4;
  auto *driver = new (driverStorage) WasmAudioDriver(settings);
  driver->SetMixerVolume(mixerVolume);
  ApplyConfiguration(*driver);
  auto *output = new (outputStorage) AudioOutDriver(*driver);
  AddOutput(*output);
  initialized_ = true;
  driverReady_.store(true, std::memory_order_release);
  if (State() != WasmAudioState::Failed) {
    SetState(WasmAudioState::Locked);
  }
}

void WasmAudio::Close() {
  for (AudioOut *output : Outputs()) {
    if (output != nullptr) {
      output->Close();
    }
  }
}

int WasmAudio::GetMixerVolume() {
  return static_cast<int>(volume_.load(std::memory_order_acquire));
}

void WasmAudio::SetMixerVolume(int volume) {
  const int clamped = std::clamp(volume, 0, 100);
  volume_.store(static_cast<std::uint32_t>(clamped),
                std::memory_order_release);
  if (auto *driver = WasmAudioDriver::Instance()) {
    driver->SetMixerVolume(clamped);
  }
}

void WasmAudio::Configure(std::uint32_t targetFillFrames,
                          std::uint32_t outputGainQ16) noexcept {
  const std::uint32_t target = std::clamp(
      targetFillFrames, WasmAudioDriver::MinimumTargetFillFrames,
      WasmAudioDriver::MaximumTargetFillFrames);
  const std::uint32_t gain =
      std::min(outputGainQ16, WasmAudioDriver::UnityGainQ16);
  configuredAudio_.store(target | (gain << ConfiguredTargetBits),
                         std::memory_order_release);
  if (auto *driver = WasmAudioDriver::Instance()) {
    ApplyConfiguration(*driver);
  }
}

void WasmAudio::ApplyConfiguration(WasmAudioDriver &driver) noexcept {
  for (;;) {
    const std::uint32_t configured =
        configuredAudio_.load(std::memory_order_acquire);
    driver.Configure(configured & ConfiguredTargetMask,
                     configured >> ConfiguredTargetBits);
    if (configured == configuredAudio_.load(std::memory_order_acquire)) {
      return;
    }
  }
}

bool WasmAudio::Unlock() noexcept {
#ifdef __EMSCRIPTEN__
  unlockOnBrowserMainThread_.store(
      emscripten_is_main_browser_thread() ? 1U : 2U,
      std::memory_order_release);
#endif
  const auto current = State();
  if (current == WasmAudioState::Unavailable || current == WasmAudioState::Failed ||
      current == WasmAudioState::Stopped) {
    return false;
  }
  if (current == WasmAudioState::Running ||
      current == WasmAudioState::Starting ||
      current == WasmAudioState::Suspended) {
    const int context = context_.load(std::memory_order_acquire);
    if (context <= 0) {
      MarkFailed(
          "The browser AudioContext is unavailable. Restart the runtime and retry.");
      return false;
    }
    // WebKit can stop worklet callbacks after backgrounding without first
    // publishing a suspended context, so the cached state may still be
    // Running. WebAudio resume() is idempotent; repeat it from each trusted
    // Web gesture and retain Running until the callback reports otherwise.
    if (current != WasmAudioState::Running) {
      SetState(WasmAudioState::Starting);
    }
#ifdef __EMSCRIPTEN__
    emscripten_resume_audio_context_async(context, ContextResumed, nullptr);
#endif
    return true;
  }
  bool expected = false;
  if (!unlockStarted_.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel)) {
    return true;
  }
  const int context = context_.load(std::memory_order_acquire);
  if (context <= 0) {
    MarkFailed("The browser AudioContext is unavailable. Restart the runtime and retry.");
    return false;
  }
  SetState(WasmAudioState::Starting);
#ifdef __EMSCRIPTEN__
  emscripten_resume_audio_context_async(context, ContextResumed, nullptr);
#endif
  return true;
}

void WasmAudio::StopBrowserAudio() noexcept {
  if (auto *driver = WasmAudioDriver::Instance()) {
    driver->DisableProducerForTeardown();
  }
  if (teardownRequested_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
#ifdef __EMSCRIPTEN__
  // With PROXY_TO_PTHREAD the application is a worker, while Emscripten's
  // main runtime stays on browser main. Never touch browser-owned handles
  // directly from the application pthread; queue the exact main-runtime
  // teardown callback and let it acknowledge completion atomically.
  if (!emscripten_is_main_runtime_thread()) {
    emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_V,
                                                BrowserMainTeardown);
    return;
  }
#endif
  BrowserMainTeardown();
}

bool WasmAudio::BrowserTeardownComplete() noexcept {
  return teardownComplete_.load(std::memory_order_acquire);
}

void WasmAudio::BrowserMainTeardown() noexcept {
  if (browserStopped_.exchange(true, std::memory_order_acq_rel)) {
    teardownComplete_.store(true, std::memory_order_release);
    return;
  }
#ifdef __EMSCRIPTEN__
  const int node = workletNode_.exchange(0, std::memory_order_acq_rel);
  if (node > 0) {
    emscripten_destroy_web_audio_node(node);
  }
  const int context = context_.exchange(0, std::memory_order_acq_rel);
  if (context > 0) {
    emscripten_destroy_audio_context(context);
  }
#endif
  // A watchdog failure remains actionable after its browser handles have been
  // released; do not overwrite it with the ordinary stop terminal state.
  if (State() != WasmAudioState::Failed) {
    SetState(WasmAudioState::Stopped);
  }
  teardownComplete_.store(true, std::memory_order_release);
}

WasmAudioState WasmAudio::State() noexcept {
  // This may be read by the application pthread and by the JS bridge.  Do
  // not synchronously query an AudioContext here: Emscripten would proxy that
  // browser-owned operation to the main thread and can deadlock startup while
  // the application is constructing its UI.  WebAudio callbacks own context
  // transitions and publish them through state_ instead.
  return state_.load(std::memory_order_acquire);
}

const char *WasmAudio::LastError() noexcept {
  for (std::size_t index = 0; index < ErrorWords; ++index) {
    const auto word = errorSnapshot_[index].load(std::memory_order_acquire);
    for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
      errorCopy_[index * sizeof(word) + byte] =
          static_cast<char>((word >> (byte * 8U)) & 0xFFU);
    }
  }
  errorCopy_.back() = '\0';
  return errorCopy_.data();
}

const WasmAudioMetrics *WasmAudio::CopyMetrics() noexcept {
  for (;;) {
    const std::uint32_t before =
        metricsSnapshot_[0].load(std::memory_order_acquire);
    if ((before & 1U) != 0U) {
      continue;
    }
    auto *words = reinterpret_cast<std::uint32_t *>(&metricsCopy);
    for (std::size_t index = 0; index < MetricsWords; ++index) {
      words[index] = metricsSnapshot_[index + 1U].load(std::memory_order_relaxed);
    }
    const std::uint32_t after =
        metricsSnapshot_[0].load(std::memory_order_acquire);
    if (before == after && (after & 1U) == 0U) {
      break;
    }
  }
  return &metricsCopy;
}

const std::uint32_t *WasmAudio::MetricsSnapshotAddress() noexcept {
  return reinterpret_cast<const std::uint32_t *>(metricsSnapshot_.data());
}

const std::uint32_t *WasmAudio::ErrorSnapshotAddress() noexcept {
  return reinterpret_cast<const std::uint32_t *>(errorSnapshot_.data());
}

void WasmAudio::PublishSnapshot() noexcept {
  WasmAudioMetrics metrics{};
  if (auto *driver = WasmAudioDriver::Instance()) {
    metrics = driver->Metrics();
  }
  metrics.state = static_cast<std::uint32_t>(State());
  metrics.setupPhase = setupPhase_.load(std::memory_order_acquire);
  metrics.unlockOnBrowserMainThread =
      unlockOnBrowserMainThread_.load(std::memory_order_acquire);
  // Publish audio diagnostics from the application snapshot boundary. The
  // realtime AudioWorklet callback only samples its monotonic clock and
  // updates fixed lock-free source atomics; it never writes trace records.
  WasmProfiler::Emit(WasmTraceCategory::Audio, WasmTraceName::AudioSnapshot,
                     WasmTracePhase::Counter, metrics.ringFillFrames);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioCallbackCount,
                     WasmTracePhase::Counter, metrics.callbackCount);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioUnderrunFrames,
                     WasmTracePhase::Counter, metrics.underrunFrames);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioOverrunFrames,
                     WasmTracePhase::Counter, metrics.overrunFrames);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioRenderDurationUs,
                     WasmTracePhase::Counter, metrics.renderMicros);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioCallbackDurationUs,
                     WasmTracePhase::Counter, metrics.callbackMicros);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioCallbackMaxDurationUs,
                     WasmTracePhase::Counter, metrics.callbackMaxMicros);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioCallbackDeadlineUs,
                     WasmTracePhase::Counter,
                     metrics.callbackDeadlineMicros);
  WasmProfiler::Emit(WasmTraceCategory::Audio,
                     WasmTraceName::AudioCallbackProcessingDeadlineMisses,
                     WasmTracePhase::Counter,
                     metrics.callbackDeadlineMisses);
  const std::uint32_t writing =
      metricsSnapshot_[0].fetch_add(1U, std::memory_order_acq_rel) + 1U;
  const auto *words = reinterpret_cast<const std::uint32_t *>(&metrics);
  for (std::size_t index = 0; index < MetricsWords; ++index) {
    metricsSnapshot_[index + 1U].store(words[index], std::memory_order_relaxed);
  }
  metricsSnapshot_[0].store(writing + 1U, std::memory_order_release);
}

void WasmAudio::MarkRunning() noexcept {
  // This is called from the AudioWorklet realtime callback. Do not query the
  // browser context or copy metrics here: a single atomic state transition is
  // the entire realtime boundary. The application rAF publishes the shared
  // diagnostics snapshot on its next normal frame.
  if (state_.load(std::memory_order_acquire) != WasmAudioState::Stopped) {
    state_.store(WasmAudioState::Running, std::memory_order_release);
  }
}

void WasmAudio::MarkFailed(const char *message) noexcept {
  if (state_.load(std::memory_order_acquire) == WasmAudioState::Stopped) {
    return;
  }
  SetError(message);
  SetState(WasmAudioState::Failed);
}

void WasmAudio::SetState(WasmAudioState state) noexcept {
  state_.store(state, std::memory_order_release);
}

void WasmAudio::AdvanceSetupPhase(std::uint32_t phase) noexcept {
  std::uint32_t current = setupPhase_.load(std::memory_order_acquire);
  while (current < phase &&
         !setupPhase_.compare_exchange_weak(current, phase,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
  }
}

void WasmAudio::SetError(const char *message) noexcept {
  const char *text = message == nullptr ? "Audio initialization failed" : message;
  bool ended = false;
  for (std::size_t index = 0; index < ErrorWords; ++index) {
    std::uint32_t word = 0U;
    for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
      const auto offset = index * sizeof(word) + byte;
      if (!ended && offset < ErrorBytes - 1U && text[offset] != '\0') {
        word |= static_cast<std::uint32_t>(
                    static_cast<unsigned char>(text[offset]))
                << (byte * 8U);
      } else {
        ended = true;
      }
    }
    errorSnapshot_[index].store(word, std::memory_order_release);
  }
}

void WasmAudio::BootstrapBrowserMain() noexcept {
#ifdef __EMSCRIPTEN__
  browserStopped_.store(false, std::memory_order_release);
  teardownRequested_.store(false, std::memory_order_release);
  teardownComplete_.store(false, std::memory_order_release);
  unlockStarted_.store(false, std::memory_order_release);
  scopeReady_.store(false, std::memory_order_release);
  driverReady_.store(false, std::memory_order_release);
  processorRequested_.store(false, std::memory_order_release);
  setupWatchdogTicks_.store(0U, std::memory_order_release);
  processorRequestedTick_.store(0U, std::memory_order_release);
  setupPhase_.store(1U, std::memory_order_release);
  unlockOnBrowserMainThread_.store(0U, std::memory_order_release);
  EmscriptenWebAudioCreateAttributes attributes{};
  attributes.latencyHint = "interactive";
  attributes.sampleRate = 44100U;
  attributes.renderSizeHint = AUDIO_CONTEXT_RENDER_SIZE_DEFAULT;
  const EMSCRIPTEN_WEBAUDIO_T context = emscripten_create_audio_context(&attributes);
  if (context <= 0) {
    MarkFailed("The browser could not create an AudioContext. Use Chrome or Edge and retry.");
    return;
  }
  context_.store(context, std::memory_order_release);
  setupPhase_.store(2U, std::memory_order_release);
  SetState(WasmAudioState::Locked);
  // This must happen in browser C main, before it yields to the application
  // pthread. Emscripten 6.0.5's hybrid worklet bootstrap synchronously
  // initializes pthread metadata and is not safe as a late exported call.
  emscripten_start_wasm_audio_worklet_thread_async(
      context, workletStack, sizeof(workletStack), WorkletThreadStarted, nullptr);
  setupPhase_.store(3U, std::memory_order_release);
  emscripten_set_timeout(PumpBrowserMainSetup, 20.0, nullptr);
#else
  MarkFailed("Browser AudioWorklet support is unavailable in this build.");
#endif
}

void WasmAudio::MarkUnavailable() noexcept {
  browserStopped_.store(true, std::memory_order_release);
  teardownRequested_.store(true, std::memory_order_release);
  teardownComplete_.store(true, std::memory_order_release);
  unlockStarted_.store(false, std::memory_order_release);
  SetError("Audio disabled; enable low-latency audio and reload.");
  SetState(WasmAudioState::Failed);
}

void WasmAudio::PumpBrowserMainSetup(void *) noexcept {
#ifdef __EMSCRIPTEN__
  if (browserStopped_.load(std::memory_order_acquire)) {
    return;
  }
  if (State() == WasmAudioState::Failed) {
    return;
  }
  constexpr std::uint32_t watchdogLimit = 500U; // 500 * 20ms = 10 seconds.
  const std::uint32_t tick =
      setupWatchdogTicks_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
  const bool scopeReady = scopeReady_.load(std::memory_order_acquire);
  const bool driverReady = driverReady_.load(std::memory_order_acquire);
  if (!scopeReady || !driverReady) {
    if (tick >= watchdogLimit) {
      MarkFailed("AudioWorklet setup timed out. Check browser audio support and restart the runtime.");
      StopBrowserAudio();
      return;
    }
    emscripten_set_timeout(PumpBrowserMainSetup, 20.0, nullptr);
    return;
  }
  const bool requested = processorRequested_.load(std::memory_order_acquire);
  const std::uint32_t requestedTick =
      processorRequestedTick_.load(std::memory_order_acquire);
  if (requested && workletNode_.load(std::memory_order_acquire) <= 0) {
    if (tick - requestedTick >= watchdogLimit) {
      MarkFailed("AudioWorklet processor setup timed out. Check browser audio support and restart the runtime.");
      StopBrowserAudio();
      return;
    }
    emscripten_set_timeout(PumpBrowserMainSetup, 20.0, nullptr);
    return;
  }
  if (!unlockStarted_.load(std::memory_order_acquire)) {
    emscripten_set_timeout(PumpBrowserMainSetup, 20.0, nullptr);
    return;
  }
  bool expected = false;
  if (!processorRequested_.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
    return;
  }
  processorRequestedTick_.store(tick, std::memory_order_release);
  setupPhase_.store(6U, std::memory_order_release);
  WebAudioWorkletProcessorCreateOptions options{};
  options.name = "picotracker-pcm";
  emscripten_create_wasm_audio_worklet_processor_async(
      context_.load(std::memory_order_acquire), &options, ProcessorCreated, nullptr);
  emscripten_set_timeout(PumpBrowserMainSetup, 20.0, nullptr);
#endif
}

void WasmAudio::OnContextResumed(int state) noexcept {
#ifdef __EMSCRIPTEN__
  if (browserStopped_.load(std::memory_order_acquire)) {
    return;
  }
  if (state == AUDIO_CONTEXT_STATE_CLOSED) {
    MarkFailed("The AudioContext closed before playback could begin.");
    StopBrowserAudio();
  } else if (state == AUDIO_CONTEXT_STATE_SUSPENDED ||
             state == AUDIO_CONTEXT_STATE_INTERRUPTED) {
    SetState(WasmAudioState::Suspended);
  } else {
    AdvanceSetupPhase(4U);
    // Timers scheduled before PROXY_TO_PTHREAD transfers C main are not a
    // reliable late-unlock wakeup in every Chrome/Emscripten combination.
    // The resume callback is guaranteed to run on browser main after the user
    // gesture, so drive the same idempotent setup pump directly here.
    PumpBrowserMainSetup(nullptr);
  }
#else
  (void)state;
#endif
}

void WasmAudio::OnWorkletThreadStarted(bool success) noexcept {
#ifdef __EMSCRIPTEN__
  if (browserStopped_.load(std::memory_order_acquire)) {
    return;
  }
  if (!success) {
    MarkFailed("The browser could not start the WASM AudioWorklet thread.");
    StopBrowserAudio();
    return;
  }
  setupPhase_.store(5U, std::memory_order_release);
  scopeReady_.store(true, std::memory_order_release);
  // Whichever asynchronous prerequisite completes last must wake processor
  // creation. processorRequested_'s CAS keeps this safe when both callbacks
  // arrive close together.
  PumpBrowserMainSetup(nullptr);
#else
  (void)success;
#endif
}

void WasmAudio::OnProcessorCreated(bool success) noexcept {
#ifdef __EMSCRIPTEN__
  if (browserStopped_.load(std::memory_order_acquire)) {
    return;
  }
  if (!success) {
    MarkFailed("The browser could not create the WASM AudioWorklet processor.");
    StopBrowserAudio();
    return;
  }
  setupPhase_.store(7U, std::memory_order_release);
  auto *driver = WasmAudioDriver::Instance();
  const int context = context_.load(std::memory_order_acquire);
  if (driver == nullptr || context <= 0) {
    MarkFailed("Audio driver stopped before the worklet was ready.");
    StopBrowserAudio();
    return;
  }
  const std::uint32_t rate = static_cast<std::uint32_t>(std::max(
      0, emscripten_audio_context_sample_rate(context)));
  driver->SetDestinationRate(rate);
  renderer = new (rendererStorage) WasmAudioWorkletRenderer(*driver, rate);
  int outputChannels[1] = {2};
  EmscriptenAudioWorkletNodeCreateOptions options{};
  options.numberOfInputs = 0;
  options.numberOfOutputs = 1;
  options.outputChannelCounts = outputChannels;
  options.channelCount = 2U;
  options.channelCountMode = WEBAUDIO_CHANNEL_COUNT_MODE_EXPLICIT;
  options.channelInterpretation = WEBAUDIO_CHANNEL_INTERPRETATION_DISCRETE;
  const EMSCRIPTEN_WEBAUDIO_T node = emscripten_create_wasm_audio_worklet_node(
      context, "picotracker-pcm", &options, PicoTracker_Wasm_AudioWorkletProcess,
      renderer);
  if (node <= 0) {
    MarkFailed("The browser could not attach the WASM AudioWorklet node.");
    StopBrowserAudio();
    return;
  }
  workletNode_.store(node, std::memory_order_release);
  emscripten_audio_node_connect(node, context, 0, 0);
  setupPhase_.store(8U, std::memory_order_release);
#else
  (void)success;
#endif
}

void Record(void *) {}
bool StartRecording(const char *, std::uint8_t, std::uint32_t) { return false; }
void StopRecording() {}
void RequestStopRecording() {}
bool WaitForRecordingStop(std::uint32_t) { return true; }
void FinishStopRecording() {}
void StartMonitoring() {}
void StopMonitoring() {}
void SetInputSource(RecordSource) {}
void SetLineInGain(std::uint8_t) {}
void SetMicGain(std::uint8_t) {}
bool IsRecordingAvailable() { return false; }
bool IsRecordingActive() { return false; }
bool IsSavingRecording() { return false; }
std::uint8_t GetSavingProgressPercent() { return 0; }
bool DidLastRecordingCaptureAudio() { return false; }

void WasmAudio_BootstrapBrowserMain() noexcept {
  WasmAudio::BootstrapBrowserMain();
}
void WasmAudio_MarkUnavailable() noexcept { WasmAudio::MarkUnavailable(); }
void WasmAudio_Configure(std::uint32_t targetFillFrames,
                         std::uint32_t outputGainQ16) noexcept {
  WasmAudio::Configure(targetFillFrames, outputGainQ16);
}
bool WasmAudio_Unlock() noexcept { return WasmAudio::Unlock(); }
void WasmAudio_Stop() noexcept { WasmAudio::StopBrowserAudio(); }
void WasmAudio_MarkRunning() noexcept { WasmAudio::MarkRunning(); }
WasmAudioState WasmAudio_GetState() noexcept { return WasmAudio::State(); }
const char *WasmAudio_GetError() noexcept { return WasmAudio::LastError(); }
const WasmAudioMetrics *WasmAudio_CopyMetrics() noexcept {
  return WasmAudio::CopyMetrics();
}
const std::uint32_t *WasmAudio_MetricsSnapshotAddress() noexcept {
  return WasmAudio::MetricsSnapshotAddress();
}
const std::uint32_t *WasmAudio_ErrorSnapshotAddress() noexcept {
  return WasmAudio::ErrorSnapshotAddress();
}
const WasmBrowserSnapshots *Wasm_BrowserSnapshots() noexcept {
  return &browserSnapshots;
}
