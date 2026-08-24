/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/gui/WasmEventManager.h"
#include "Adapters/wasm/gui/WasmViewDiagnostics.h"

#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/gui/WasmUi2Control.h"
#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/audio/WasmAudioDriver.h"
#include "Adapters/wasm/filesystem/WasmStorageBridge.h"
#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/midi/WasmMidiService.h"
#include "Adapters/wasm/platform/WasmApplicationSnapshot.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Adapters/wasm/tracing/WasmProfiler.h"
#include "Application/Application.h"
#include "Application/Views/ViewData.h"
#include "Application/AppWindow.h"
#include "Application/UI2/Ui2LegacyApplicationStateSource.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Project.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Player/Player.h"
#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

#include <SDL.h>
#include <emscripten/emscripten.h>

namespace {
void PublishApplicationSnapshot(AppWindow &window) noexcept {
  Project &project = window.ProjectForDiagnostics();
  char projectName[MAX_PROJECT_NAME_LENGTH + 1U]{};
  project.GetProjectName(projectName);

  std::uint32_t sampleCount = 0U;
  if (auto *pool = SamplePool::GetInstance()) {
    const int loaded = pool->GetNameListSize();
    if (loaded > 0) {
      sampleCount = static_cast<std::uint32_t>(loaded);
    }
  }

  bool playerRunning = false;
  std::uint32_t masterLevel = 0U;
  if (window.PlayerInitializedForDiagnostics()) {
    Player *player = Player::GetInstance();
    playerRunning = player->IsRunning();
    // Mixer levels are produced and sampled on this same application pthread.
    // Do not query Player or MixerService from browser main.
    if (playerRunning) {
      masterLevel = player->GetMasterLevel();
    }
  }

  Wasm_ApplicationSnapshot().Publish(
      projectName, static_cast<std::uint32_t>(project.GetTempo()), sampleCount,
      playerRunning, masterLevel);
}
} // namespace

bool WasmEventManager::Init() {
  if (!EventManager::Init()) {
    return false;
  }
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
  diagnosticModalAwaitingDraw_ = NoDiagnosticModal;
  ui2Enabled_.store(false, std::memory_order_release);
  ui2Runtime_.reset();
  ui2Active_ = false;
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
  WASM_TRACE_SCOPE(WasmTraceCategory::Ui, WasmTraceName::Frame);

  GUIWindow *window = Application::GetInstance()->GetWindow();
  if (window == nullptr) {
    PicoTracker_Wasm_Fail("PicoTracker application did not create a window");
    StopRuntime();
    return;
  }
  auto *wasmWindow = static_cast<WasmGUIWindowImp *>(window->GetImpWindow());
  if (wasmWindow == nullptr) {
    PicoTracker_Wasm_Fail("PicoTracker did not create a browser window");
    StopRuntime();
    return;
  }
  if (!ui2Runtime_.has_value()) {
    ui2Runtime_.emplace(*wasmWindow);
  }
  ui2::UiLegacyApplicationStateSource ui2Source(
      *static_cast<AppWindow *>(window));
  const auto ui2ShouldOwnDisplay = [&]() {
    return ui2Enabled_.load(std::memory_order_acquire) &&
           ui2Runtime_->Supports(ui2Source);
  };
  const auto presentUi2 = [&]() {
    const bool supported = ui2ShouldOwnDisplay();
    wasmWindow->SetUi2DisplayOwnership(supported);
    if (!supported) {
      ui2Active_ = false;
      return;
    }
    const bool entering = !ui2Active_;
    if (entering) ui2Runtime_->Invalidate();
    const ui2::PresentResult result =
        ui2Runtime_->Present(ui2Source);
    if (result == ui2::PresentResult::Failed) {
      ui2Enabled_.store(false, std::memory_order_release);
      wasmWindow->SetUi2DisplayOwnership(false);
      ui2Active_ = false;
    } else if (result == ui2::PresentResult::Presented) {
      ui2Active_ = true;
    }
  };
  // Establish ownership before legacy event/tick drawing. UI2 pages keep the
  // legacy View alive for behavior, but only the UI2 presenter may publish.
  wasmWindow->SetUi2DisplayOwnership(ui2ShouldOwnDisplay());
  if (booting_) {
    // The browser main and application pthread both get a scheduling turn
    // before the first expensive UI clock tick. This avoids startup races in
    // SDL/OffscreenCanvas while preserving the normal frame lifecycle.
    {
      WASM_TRACE_SCOPE(WasmTraceCategory::Ui, WasmTraceName::UiUpdate);
      window->Update(true);
    }
    {
      WASM_TRACE_SCOPE(WasmTraceCategory::Ui, WasmTraceName::ClockTick);
      window->ClockTick();
    }
    presentUi2();
    if (!wasmWindow->HasPresentedFrame()) {
      PicoTracker_Wasm_Fail("PicoTracker did not present an initial browser frame");
      StopRuntime();
      return;
    }
    // This application rAF is the only writer of the browser metrics
    // seqlock. Browser-main setup/teardown and the realtime callback publish
    // source atomics only, so there can be no competing writer.
    WasmAudio::PublishSnapshot();
    PublishApplicationSnapshot(*static_cast<AppWindow *>(window));
    WasmStorage_FlushMutationNotifications();
    PicoTracker_Wasm_MarkReady();
    booting_ = false;
    nextTick_ = SDL_GetTicks64() + PICO_CLOCK_INTERVAL;
    return;
  }
  if (runtimeState != static_cast<std::uint32_t>(WasmRuntimeState::Ready)) {
    StopRuntime();
    return;
  }

  auto *appWindow = static_cast<AppWindow *>(window);
  const std::uint32_t requestedView = requestedDiagnosticView_.exchange(
      NoDiagnosticView, std::memory_order_acq_rel);
  if (requestedView != NoDiagnosticView) {
    if (appWindow->SwitchViewForDiagnostics(requestedView)) {
      // Publish only after the next normal ClockTick has returned, proving the
      // selected view's DrawView path ran on the application pthread.
      diagnosticViewAwaitingDraw_ = requestedView;
    } else {
      diagnosticView_.store(NoDiagnosticView, std::memory_order_release);
      diagnosticViewGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }
  }

  const std::uint32_t requestedModal = requestedDiagnosticModal_.exchange(
      NoDiagnosticModal, std::memory_order_acq_rel);
  if (requestedModal != NoDiagnosticModal) {
    if (appWindow->SwitchModalForDiagnostics(requestedModal)) {
      // The modal-count sentinel is the close request. Open and close are
      // published only after the regular ClockTick redraw path returns.
      diagnosticModalAwaitingDraw_ = requestedModal;
    } else {
      diagnosticModal_.store(NoDiagnosticModal, std::memory_order_release);
      diagnosticModalGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }
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
      wasmWindow->ProcessQuit();
      break;
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
        wasmWindow->ProcessExpose();
      }
      break;
    case SDL_USEREVENT: {
      if (event.user.code == InputMap::ActionEventCode) {
        std::uint16_t action = 0;
        bool pressed = false;
        if (InputMap::DecodeActionEvent(
                reinterpret_cast<std::uintptr_t>(event.user.data1), action,
                pressed)) {
          GUIEvent guiEvent(static_cast<long>(action),
                            pressed ? ET_PADBUTTONDOWN : ET_PADBUTTONUP,
                            System::GetInstance()->GetClock(), false, false,
                            false);
          {
            WASM_TRACE_SCOPE(WasmTraceCategory::Input,
                             WasmTraceName::InputDispatch);
            window->DispatchEvent(guiEvent);
          }
          if (diagnosticView_.load(std::memory_order_acquire) !=
              NoDiagnosticView) {
            // DispatchEvent returning proves the active C++ view processed the
            // input; InputMap's generation alone only proves queue delivery.
            diagnosticInputGeneration_.fetch_add(1, std::memory_order_acq_rel);
          }
          InputMap::AcknowledgeAction(action, pressed);
        }
      } else {
        auto *guiEvent = static_cast<GUIEvent *>(event.user.data1);
        if (guiEvent != nullptr) {
          {
            WASM_TRACE_SCOPE(WasmTraceCategory::Input,
                             WasmTraceName::InputDispatch);
            window->DispatchEvent(*guiEvent);
          }
          delete guiEvent;
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
    WASM_TRACE_SCOPE(WasmTraceCategory::Ui, WasmTraceName::ClockTick);
    window->ClockTick();
    presentUi2();
    if (diagnosticViewAwaitingDraw_ != NoDiagnosticView) {
      diagnosticView_.store(appWindow->CurrentViewForDiagnostics(),
                            std::memory_order_release);
      diagnosticViewGeneration_.fetch_add(1, std::memory_order_acq_rel);
      diagnosticViewAwaitingDraw_ = NoDiagnosticView;
    } else if (diagnosticView_.load(std::memory_order_acquire) !=
               NoDiagnosticView) {
      diagnosticView_.store(appWindow->CurrentViewForDiagnostics(),
                            std::memory_order_release);
    }
    if (diagnosticModalAwaitingDraw_ != NoDiagnosticModal) {
      diagnosticModal_.store(appWindow->CurrentModalForDiagnostics(),
                             std::memory_order_release);
      diagnosticModalGeneration_.fetch_add(1, std::memory_order_acq_rel);
      diagnosticModalAwaitingDraw_ = NoDiagnosticModal;
    } else if (diagnosticModal_.load(std::memory_order_acquire) !=
               NoDiagnosticModal) {
      diagnosticModal_.store(appWindow->CurrentModalForDiagnostics(),
                             std::memory_order_release);
    }
    nextTick_ = now + PICO_CLOCK_INTERVAL;
  }
  PublishApplicationSnapshot(*appWindow);
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

void WasmEventManager::SetUi2Enabled(bool enabled) {
  ui2Enabled_.store(enabled, std::memory_order_release);
}

bool WasmEventManager::Ui2Enabled() const {
  return ui2Enabled_.load(std::memory_order_acquire);
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

void WasmUi2_SetEnabled(bool enabled) noexcept {
  WasmEventManager::GetInstance()->SetUi2Enabled(enabled);
}

bool WasmUi2_IsEnabled() noexcept {
  return WasmEventManager::GetInstance()->Ui2Enabled();
}
