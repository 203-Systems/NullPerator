#include "Adapters/esp32/platform/platform.h"
#include "Adapters/esp32/system/picoTrackerSystem.h"
#include "Application/Application.h"
#include "tusb.h"

void app_main(void)
{

  // Initialise microcontroller specific hardware
  // board_init();

  // Initialise TinyUSB
  tusb_init();

  // Do remaining pT init, this needs to be done *after* above hardware and
  // tinyusb subsystem init
  platform_init();

  picoTrackerSystem::Boot(0, NULL);

  GUICreateWindowParams params;
  params.title = "picoTracker";

  Application::GetInstance()->Init(params);

  picoTrackerSystem::MainLoop();
  printf("Finish main loop?\n");

  picoTrackerSystem::Shutdown();
}
