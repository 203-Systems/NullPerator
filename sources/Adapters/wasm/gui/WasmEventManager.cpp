/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/gui/WasmEventManager.h"
#include "Adapters/wasm/gui/WasmViewDiagnostics.h"

#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/audio/WasmAudioDriver.h"
#include "Adapters/wasm/filesystem/WasmStorageBridge.h"
#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/midi/WasmMidiService.h"
#include "Adapters/wasm/platform/WasmApplicationSnapshot.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Adapters/wasm/tracing/WasmProfiler.h"
#include "Application/UI2/Ui2TrackerApplication.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Project.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Player/Player.h"
#include "System/System/System.h"
#include <SDL.h>
#include <emscripten/emscripten.h>

namespace {
constexpr double kFrameIntervalMs = 33.0;

void PublishApplicationSnapshot(
    ui2::Ui2TrackerApplication &application) noexcept {
  Project &project = application.Session().ProjectModel();
  char projectName[MAX_PROJECT_NAME_LENGTH + 1U]{};
  project.GetProjectName(projectName);
  std::uint32_t sampleCount = 0U;
  if (auto *pool = SamplePool::GetInstance()) {
    const int loaded = pool->GetNameListSize();
    sampleCount = loaded > 0 ? static_cast<std::uint32_t>(loaded) : 0U;
  }
  Player *player = Player::GetInstance();
  const bool running = application.Session().PlayerInitialized() &&
                       player != nullptr && player->IsRunning();
  Wasm_ApplicationSnapshot().Publish(
      projectName, static_cast<std::uint32_t>(project.GetTempo()), sampleCount,
      running, running ? player->GetMasterLevel() : 0U);
}

ui2::UiApplicationPage NativePageForDiagnostic(std::uint32_t view) {
  using Page = ui2::UiApplicationPage;
  switch (view) {
  case VT_SONG:
    return Page::Song;
  case VT_CHAIN:
    return Page::Chain;
  case VT_PHRASE:
    return Page::Phrase;
  case VT_PROJECT:
    return Page::Project;
  case VT_DEVICE:
    return Page::Device;
  case VT_INSTRUMENT:
    return Page::Instrument;
  case VT_TABLE:
  case VT_TABLE2:
    return Page::Table;
  case VT_GROOVE:
    return Page::Groove;
  case VT_MIXER:
    return Page::Mixer;
  case VT_IMPORT:
  case VT_INSTRUMENT_IMPORT:
  case VT_SELECTPROJECT:
  case VT_SELECTTHEME:
  case VT_THEME_IMPORT:
    return Page::Browser;
  case VT_THEME:
    return Page::Theme;
  case VT_SAMPLE_EDITOR:
    return Page::SampleEditor;
  case VT_SAMPLE_SLICES:
    return Page::SampleSlices;
  case VT_RECORD:
    return Page::Record;
  default:
    return Page::None;
  }
}
} // namespace

bool WasmEventManager::Init() {
  finished_.store(false, std::memory_order_release);
  runtimeStopped_ = false;
  booting_ = true;
  requestedDiagnosticView_.store(NoDiagnosticView, std::memory_order_release);
  diagnosticView_.store(NoDiagnosticView, std::memory_order_release);
  diagnosticViewGeneration_.store(0, std::memory_order_release);
  requestedDiagnosticModal_.store(NoDiagnosticModal,
                                  std::memory_order_release);
  diagnosticModal_.store(NoDiagnosticModal, std::memory_order_release);
  diagnosticModalGeneration_.store(0, std::memory_order_release);
  diagnosticInputGeneration_.store(0, std::memory_order_release);
  diagnosticViewAwaitingDraw_ = NoDiagnosticView;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
    return false;
  }
  // Browser keyboard ownership belongs exclusively to the Svelte KeyMap. SDL
  // otherwise installs document-level keyboard hooks that bypass focus and
  // remapping rules even though the compatibility canvas is hidden.
  SDL_EventState(SDL_KEYDOWN, SDL_DISABLE);
  SDL_EventState(SDL_KEYUP, SDL_DISABLE);
  return true;
}

int WasmEventManager::MainLoop() {
  nextTick_ = SDL_GetTicks64();
  emscripten_set_main_loop_arg(&WasmEventManager::RunFrame, this, 0, 1);
  return 0;
}

void WasmEventManager::RunFrame(void *context) {
  static_cast<WasmEventManager *>(context)->PumpFrame();
}

void WasmEventManager::PumpFrame() {
  if (runtimeStopped_) {
    return;
  }
  const auto runtimeState = PicoTracker_Wasm_GetState();
  if (finished_.load(std::memory_order_acquire) ||
      runtimeState == static_cast<std::uint32_t>(WasmRuntimeState::Stopping) ||
      runtimeState == static_cast<std::uint32_t>(WasmRuntimeState::Stopped) ||
      runtimeState == static_cast<std::uint32_t>(WasmRuntimeState::Failed)) {
    StopRuntime();
    return;
  }
  if (nativeApplication_ == nullptr || nativeWindow_ == nullptr) {
    PicoTracker_Wasm_Fail("Native UI2 application was not configured");
    StopRuntime();
    return;
  }
  WASM_TRACE_SCOPE(WasmTraceCategory::Ui, WasmTraceName::Frame);
  WasmGUIWindowImp &window = *nativeWindow_;
  ui2::Ui2TrackerApplication &application = *nativeApplication_;
  if (booting_) {
    application.Invalidate();
    if (application.Present() == ui2::PresentResult::Failed ||
        !window.HasPresentedFrame()) {
      PicoTracker_Wasm_Fail("PicoTracker did not present an initial UI2 frame");
      StopRuntime();
      return;
    }
    WasmAudio::PublishSnapshot();
    PublishApplicationSnapshot(application);
    WasmStorage_FlushMutationNotifications();
    PicoTracker_Wasm_MarkReady();
    booting_ = false;
    nextTick_ = SDL_GetTicks64() + kFrameIntervalMs;
    return;
  }
  if (runtimeState != static_cast<std::uint32_t>(WasmRuntimeState::Ready)) {
    StopRuntime();
    return;
  }

  const std::uint32_t requestedView = requestedDiagnosticView_.exchange(
      NoDiagnosticView, std::memory_order_acq_rel);
  if (requestedView != NoDiagnosticView) {
    const ui2::UiApplicationPage page = NativePageForDiagnostic(requestedView);
    if (page != ui2::UiApplicationPage::None) {
      application.ActivatePage(page);
      diagnosticViewAwaitingDraw_ = requestedView;
    } else {
      diagnosticView_.store(NoDiagnosticView, std::memory_order_release);
      diagnosticViewGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }
  }

  const std::uint32_t requestedModal = requestedDiagnosticModal_.exchange(
      NoDiagnosticModal, std::memory_order_acq_rel);
  if (requestedModal != NoDiagnosticModal) {
    diagnosticModal_.store(requestedModal, std::memory_order_release);
    diagnosticModalGeneration_.fetch_add(1, std::memory_order_acq_rel);
  }

  // SDL can temporarily reject an event when its queue is full. InputMap keeps
  // the browser's latest desired state and retries it here in frame order.
  {
    WASM_TRACE_SCOPE(WasmTraceCategory::Input, WasmTraceName::InputRetry);
    InputMap::RetryPendingTransitions();
  }
  if (auto *midi = WasmMidiService::Instance()) {
    midi->Poll();
  }
  if (auto *audio = WasmAudioDriver::Instance()) {
    audio->PumpProducer();
  }
  // The browser reads audio diagnostics from shared atomic words. Publish on
  // the application frame rather than from the realtime worklet callback.
  WasmAudio::PublishSnapshot();

  SDL_Event event{};
  while (SDL_PollEvent(&event) != 0) {
    switch (event.type) {
    case SDL_QUIT:
      PostQuitMessage();
      break;
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
        application.Invalidate();
      }
      break;
    case SDL_USEREVENT: {
      if (event.user.code == InputMap::ActionEventCode) {
        std::uint16_t action = 0;
        bool pressed = false;
        if (InputMap::DecodeActionEvent(
                reinterpret_cast<std::uintptr_t>(event.user.data1), action,
                pressed)) {
          application.DispatchTrackerAction(static_cast<TrackerAction>(action),
                                            pressed);
          diagnosticInputGeneration_.fetch_add(1, std::memory_order_acq_rel);
          InputMap::AcknowledgeAction(action, pressed);
        }
      }
      break;
    }
    default:
      break;
    }
  }

  const double now = SDL_GetTicks64();
  if (now >= nextTick_) {
    application.Tick(static_cast<std::uint32_t>(now));
    if (application.Present() == ui2::PresentResult::Failed) {
      PicoTracker_Wasm_Fail("UI2 frame presentation failed");
      StopRuntime();
      return;
    }
    if (diagnosticViewAwaitingDraw_ != NoDiagnosticView) {
      diagnosticView_.store(diagnosticViewAwaitingDraw_,
                            std::memory_order_release);
      diagnosticViewGeneration_.fetch_add(1, std::memory_order_acq_rel);
      diagnosticViewAwaitingDraw_ = NoDiagnosticView;
    }
    nextTick_ = now + kFrameIntervalMs;
  }
  PublishApplicationSnapshot(application);
  // DispatchEvent may perform a multi-step atomic file replacement. Only let
  // browser-main start IDBFS after the whole event batch has returned.
  WasmStorage_FlushMutationNotifications();
}

void WasmEventManager::RequestDiagnosticView(std::uint32_t viewType) {
  requestedDiagnosticView_.store(viewType, std::memory_order_release);
}

void WasmEventManager::RequestDiagnosticModal(std::uint32_t modalType) {
  requestedDiagnosticModal_.store(modalType, std::memory_order_release);
}

std::uint32_t WasmEventManager::DiagnosticView() const {
  return diagnosticView_.load(std::memory_order_acquire);
}

std::uint32_t WasmEventManager::DiagnosticViewGeneration() const {
  return diagnosticViewGeneration_.load(std::memory_order_acquire);
}

std::uint32_t WasmEventManager::DiagnosticModal() const {
  return diagnosticModal_.load(std::memory_order_acquire);
}

std::uint32_t WasmEventManager::DiagnosticModalGeneration() const {
  return diagnosticModalGeneration_.load(std::memory_order_acquire);
}

std::uint32_t WasmEventManager::DiagnosticInputGeneration() const {
  return diagnosticInputGeneration_.load(std::memory_order_acquire);
}

void WasmEventManager::ConfigureNative(
    WasmGUIWindowImp &window, ui2::Ui2TrackerApplication &application) {
  nativeWindow_ = &window;
  nativeApplication_ = &application;
}

void WasmEventManager::StopRuntime() {
  if (runtimeStopped_) {
    return;
  }
  const auto runtimeState =
      static_cast<WasmRuntimeState>(PicoTracker_Wasm_GetState());
  if (runtimeState == WasmRuntimeState::Failed) {
    // Fatal shutdown follows the same browser-main audio teardown handshake as
    // an explicit Stop. Keep the rAF alive until that callback acknowledges;
    // otherwise a later Restart can terminate a live AudioWorklet.
    WasmAudio::StopBrowserAudio();
  }
  if ((runtimeState == WasmRuntimeState::Stopping ||
       runtimeState == WasmRuntimeState::Failed) &&
      !WasmAudio::BrowserTeardownComplete()) {
    // Keep the application rAF alive until browser main has detached the
    // WebAudio graph. JavaScript only terminates the pthread after the
    // subsequent Stopped acknowledgement.
    return;
  }
  runtimeStopped_ = true;
  if (nativeApplication_ != nullptr) {
    // The native application is placement-new'd into static storage, so its
    // destructor is not invoked when the browser pthread stops. Run the
    // explicit idempotent teardown while MIDI/audio platform services still
    // exist; this is the same ordering used by embedded UI2 product mains.
    nativeApplication_->Shutdown();
  }
  // Publish the terminal audio source atomics from the same rAF-owned writer
  // before stopping this loop. It is intentionally after the browser-main
  // teardown acknowledgement above.
  WasmAudio::PublishSnapshot();
  WasmStorage_FlushMutationNotifications();
  PicoTracker_Wasm_ReleaseAllActions();
  emscripten_cancel_main_loop();
  const bool publishStopped = runtimeState == WasmRuntimeState::Stopping ||
                              runtimeState == WasmRuntimeState::Failed;
  if (publishStopped) {
    WasmSystem::ShutdownPlatformServices();
  }
  SDL_Quit();
  if (publishStopped) {
    // Stopped is published last so JavaScript can never terminate the pthread
    // while SDL or platform teardown is still executing.
    PicoTracker_Wasm_MarkStopped();
  }
}

void WasmEventManager::PostQuitMessage() {
  finished_.store(true, std::memory_order_release);
  PicoTracker_Wasm_ReleaseAllActions();
  PicoTracker_Wasm_RequestShutdown();
}

int WasmEventManager::GetKeyCode(const char *name) {
  if (name == nullptr) {
    return -1;
  }
  const SDL_Scancode scanCode = SDL_GetScancodeFromName(name);
  return scanCode == SDL_SCANCODE_UNKNOWN ? -1 : static_cast<int>(scanCode);
}

void WasmViewDiagnostics_Request(std::uint32_t viewType) noexcept {
  WasmEventManager::GetInstance()->RequestDiagnosticView(viewType);
}

std::uint32_t WasmViewDiagnostics_Current() noexcept {
  return WasmEventManager::GetInstance()->DiagnosticView();
}

std::uint32_t WasmViewDiagnostics_ViewGeneration() noexcept {
  return WasmEventManager::GetInstance()->DiagnosticViewGeneration();
}

std::uint32_t WasmViewDiagnostics_InputGeneration() noexcept {
  return WasmEventManager::GetInstance()->DiagnosticInputGeneration();
}

void WasmViewDiagnostics_RequestModal(std::uint32_t modalType) noexcept {
  WasmEventManager::GetInstance()->RequestDiagnosticModal(modalType);
}

std::uint32_t WasmViewDiagnostics_CurrentModal() noexcept {
  return WasmEventManager::GetInstance()->DiagnosticModal();
}

std::uint32_t WasmViewDiagnostics_ModalGeneration() noexcept {
  return WasmEventManager::GetInstance()->DiagnosticModalGeneration();
}
