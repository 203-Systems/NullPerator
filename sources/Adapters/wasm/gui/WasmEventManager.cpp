/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/gui/WasmEventManager.h"

#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/audio/WasmAudioDriver.h"
#include "Adapters/wasm/filesystem/WasmStorageBridge.h"
#include "Adapters/wasm/input/InputMap.h"
#include "Adapters/wasm/platform/WasmApplicationSnapshot.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Application/Application.h"
#include "Application/AppWindow.h"
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
  if (booting_) {
    // The browser main and application pthread both get a scheduling turn
    // before the first expensive UI clock tick. This avoids startup races in
    // SDL/OffscreenCanvas while preserving the normal frame lifecycle.
    window->Update(true);
    window->ClockTick();
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

  // SDL can temporarily reject an event when its queue is full. InputMap keeps
  // the browser's latest desired state and retries it here in frame order.
  InputMap::RetryPendingTransitions();
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
          window->DispatchEvent(guiEvent);
          InputMap::AcknowledgeAction(action, pressed);
        }
      } else {
        auto *guiEvent = static_cast<GUIEvent *>(event.user.data1);
        if (guiEvent != nullptr) {
          window->DispatchEvent(*guiEvent);
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
    window->ClockTick();
    nextTick_ = now + PICO_CLOCK_INTERVAL;
  }
  PublishApplicationSnapshot(*static_cast<AppWindow *>(window));
  // DispatchEvent may perform a multi-step atomic file replacement. Only let
  // browser-main start IDBFS after the whole event batch has returned.
  WasmStorage_FlushMutationNotifications();
}

void WasmEventManager::StopRuntime() {
  if (runtimeStopped_) {
    return;
  }
  if (PicoTracker_Wasm_GetState() ==
          static_cast<std::uint32_t>(WasmRuntimeState::Stopping) &&
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
  if (PicoTracker_Wasm_GetState() ==
      static_cast<std::uint32_t>(WasmRuntimeState::Stopping)) {
    WasmSystem::ShutdownPlatformServices();
    PicoTracker_Wasm_MarkStopped();
  }
  SDL_Quit();
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
