#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/gui/GUIFactory.h"
#include "Adapters/wasm/gui/WasmEventManager.h"
#include "Adapters/wasm/gui/WasmGUIWindowImp.h"
#include "Adapters/wasm/system/WasmSystem.h"
#include "Application/UI2/Ui2TrackerApplication.h"
#include "UIFramework/Interfaces/I_GUIWindowFactory.h"

#include <emscripten/emscripten.h>
#include <SDL.h>
#include <new>

namespace {
void CompleteFailedStartupShutdown(void *) {
  // Browser audio is created before PROXY_TO_PTHREAD enters C main. A failure
  // anywhere below therefore owns the same browser-main teardown handshake as
  // a running application fatal, even when EventManager never reached its
  // normal rAF loop.
  WasmAudio::StopBrowserAudio();
  if (!WasmAudio::BrowserTeardownComplete()) {
    return;
  }

  WasmSystem::ShutdownPlatformServices();
  SDL_Quit();
  // Stopped is the final acknowledgement: every native/platform resource
  // above has been released before JavaScript may terminate the pthread.
  PicoTracker_Wasm_MarkStopped();
  emscripten_cancel_main_loop();
}

int FailStartup(const char *message) {
  PicoTracker_Wasm_Fail(message);
  WasmAudio::StopBrowserAudio();
  // Keep the application pthread alive until browser main acknowledges that
  // its AudioContext/worklet graph is gone. simulate_infinite_loop mirrors the
  // normal EventManager lifecycle and prevents main from returning early.
  emscripten_set_main_loop_arg(CompleteFailedStartupShutdown, nullptr, 0, 1);
  return 1;
}
} // namespace

int main() {
  if (!WasmSystem::InstallPlatformServices()) {
    return FailStartup("Failed to install browser platform services");
  }

  alignas(GUIFactory) static unsigned char factoryStorage[sizeof(GUIFactory)];
  auto *factory = new (factoryStorage) GUIFactory();
  I_GUIWindowFactory::Install(factory);
  auto *eventManager = static_cast<WasmEventManager *>(factory->GetEventManager());
  if (eventManager == nullptr || !eventManager->Init()) {
    return FailStartup("Failed to initialize SDL2 browser UI");
  }

  GUICreateWindowParams params{};
  params.title = "PicoTracker";
  auto &window = static_cast<WasmGUIWindowImp &>(factory->CreateWindowImp(params));
  alignas(ui2::Ui2TrackerApplication) static unsigned char applicationStorage[
      sizeof(ui2::Ui2TrackerApplication)];
  auto *application =
      new (applicationStorage) ui2::Ui2TrackerApplication(window);
  if (!application->Init()) {
    return FailStartup("Failed to initialize PicoTracker application");
  }
  eventManager->ConfigureNative(window, *application);
  // MainLoop installs an Emscripten loop with simulate_infinite_loop=true, so
  // it does not return while the application pthread is alive. Teardown and
  // the final Stopped acknowledgement are owned by WasmEventManager; keeping
  // a second cleanup path here risks publishing Stopped before SDL/browser
  // resources have actually been released.
  eventManager->MainLoop();
  return 0;
}
