#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/gui/GUIFactory.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Application/Application.h"
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"

#include <emscripten/emscripten.h>
#include <new>

int main() {
  if (!WasmSystem::InstallPlatformServices()) {
    PicoTracker_Wasm_Fail("Failed to install browser platform services");
    emscripten_exit_with_live_runtime();
    return 1;
  }

  alignas(GUIFactory) static unsigned char factoryStorage[sizeof(GUIFactory)];
  auto *factory = new (factoryStorage) GUIFactory();
  I_GUIWindowFactory::Install(factory);
  EventManager *eventManager = factory->GetEventManager();
  if (eventManager == nullptr || !eventManager->Init()) {
    PicoTracker_Wasm_Fail("Failed to initialize SDL2 browser UI");
    emscripten_exit_with_live_runtime();
    return 1;
  }

  GUICreateWindowParams params{};
  params.title = "PicoTracker";
  if (!Application::GetInstance()->Init(params)) {
    PicoTracker_Wasm_Fail("Failed to initialize PicoTracker application");
    emscripten_exit_with_live_runtime();
    return 1;
  }
  GUIWindow *window = Application::GetInstance()->GetWindow();
  if (window == nullptr) {
    PicoTracker_Wasm_Fail("PicoTracker application did not create a window");
    emscripten_exit_with_live_runtime();
    return 1;
  }
  eventManager->MainLoop();
  if (PicoTracker_Wasm_GetState() ==
      static_cast<std::uint32_t>(WasmRuntimeState::Stopping)) {
    WasmSystem::ShutdownPlatformServices();
    PicoTracker_Wasm_MarkStopped();
  }
  emscripten_exit_with_live_runtime();
  return 0;
}
