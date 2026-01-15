#include "Adapters/node/platform/platform.h"
#include "Adapters/node/system/System.h"
#include "Application/Application.h"
#include "tusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

int main(int argc, char *argv[]) {
  
  // Initialise microcontroller specific hardware
  board_init();

  // Initialise TinyUSB
  tusb_init();

  // Do remaining pT init, this needs to be done *after* above hardware and
  // tinyusb subsystem init
  platform_init();

  NodeSystem::Boot(argc, argv);

  GUICreateWindowParams params;
  params.title = "picoTracker";

  Application::GetInstance()->Init(params);

  NodeSystem::MainLoop();

  NodeSystem::Shutdown();
}


extern "C" {
  int main(int argc, char *argv[]);
  void app_main(void) {
    (void)main(0, NULL);
  }
}
