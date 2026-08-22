/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/gui/WasmEventManager.h"

#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Application/Application.h"
#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"

#include <SDL.h>
#include <emscripten/emscripten.h>

bool WasmEventManager::Init() {
  if (!EventManager::Init()) {
    return false;
  }
  finished_.store(false, std::memory_order_release);
  runtimeStopped_ = false;
  return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0;
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
  if (finished_.load(std::memory_order_acquire) ||
      PicoTracker_Wasm_GetState() !=
          static_cast<std::uint32_t>(WasmRuntimeState::Ready)) {
    StopRuntime();
    return;
  }

  GUIWindow *window = Application::GetInstance()->GetWindow();
  auto *wasmWindow = static_cast<WasmGUIWindowImp *>(window->GetImpWindow());
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
      auto *guiEvent = static_cast<GUIEvent *>(event.user.data1);
      if (guiEvent != nullptr) {
        window->DispatchEvent(*guiEvent);
        delete guiEvent;
      }
      break;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      const GUIEventType type =
          event.type == SDL_KEYDOWN ? ET_KEYDOWN : ET_KEYUP;
      GUIEvent guiEvent(static_cast<long>(event.key.keysym.sym), type,
                        System::GetInstance()->GetClock(),
                        (event.key.keysym.mod & KMOD_CTRL) != 0,
                        (event.key.keysym.mod & KMOD_SHIFT) != 0, false);
      window->DispatchEvent(guiEvent);
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
}

void WasmEventManager::StopRuntime() {
  if (runtimeStopped_) {
    return;
  }
  runtimeStopped_ = true;
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
  PicoTracker_Wasm_RequestShutdown();
}

int WasmEventManager::GetKeyCode(const char *name) {
  if (name == nullptr) {
    return -1;
  }
  const SDL_Scancode scanCode = SDL_GetScancodeFromName(name);
  return scanCode == SDL_SCANCODE_UNKNOWN ? -1 : static_cast<int>(scanCode);
}
