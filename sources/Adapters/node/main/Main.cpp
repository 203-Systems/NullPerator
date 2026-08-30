#include "Adapters/node/platform/platform.h"

#include "Adapters/node/system/Ui2System.h"
#include "Adapters/node/system/TaskStackTelemetry.h"
#include "Adapters/node/ui2/NodeUi2Platform.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstddef>
#include <memory>

namespace {

constexpr char kLogTag[] = "NODE_UI2_MAIN";

int RunUi2Product(int argc, char **argv) {
  NodeTaskStackTelemetry stackTelemetry("main");
  board_init();
  platform_init();

  if (!NodeUi2System::Boot(argc, argv)) {
    ESP_LOGE(kLogTag, "Native service boot failed");
    NodeUi2System::Shutdown();
    return 1;
  }

  constexpr std::size_t kApplicationBytes =
      NodeUi2Platform::kApplicationStorageBytes;
  constexpr std::size_t kApplicationAlignment =
      NodeUi2Platform::kApplicationStorageAlignment;
  void *applicationStorage = heap_caps_aligned_alloc(
      kApplicationAlignment, kApplicationBytes,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (applicationStorage == nullptr) {
    ESP_LOGE(kLogTag,
             "Unable to reserve %u-byte UI2 application block in PSRAM",
             static_cast<unsigned>(kApplicationBytes));
    NodeUi2System::Shutdown();
    return 1;
  }

  // Keep the cross-core task/mailbox/event-group control block in internal
  // DRAM. Only the large, fixed application/model/renderer object lives in
  // PSRAM; its lifetime is joined before the PSRAM allocation is released.
  alignas(NodeUi2Platform) DRAM_ATTR static std::byte
      platformStorage[sizeof(NodeUi2Platform)];
  NodeUi2Platform *platform = std::construct_at(
      reinterpret_cast<NodeUi2Platform *>(platformStorage),
      applicationStorage, kApplicationBytes);

  int exitCode = 0;
  if (!platform->Start()) {
    ESP_LOGE(kLogTag, "UI2 platform task startup failed");
    exitCode = 1;
  } else {
    while (true) {
      const NodeUi2Platform::State state = platform->CurrentState();
      if (state == NodeUi2Platform::State::Failed ||
          state == NodeUi2Platform::State::Stopped)
        break;
      if (NodeUi2System::QuitRequested())
        platform->RequestStop();
      stackTelemetry.Poll();
      vTaskDelay(pdMS_TO_TICKS(10U));
    }
    if (platform->CurrentState() == NodeUi2Platform::State::Failed)
      exitCode = 1;
  }

  platform->RequestStop();
  // This is intentionally an unbounded join. Freeing the PSRAM application
  // block while an LCD/input/application task can still observe it is a hard
  // use-after-free; a hung shutdown must remain visible instead of corrupting
  // memory and pretending to have completed.
  (void)platform->WaitForStop(UINT32_MAX);
  std::destroy_at(platform);
  heap_caps_free(applicationStorage);
  NodeUi2System::Shutdown();
  return exitCode;
}
} // namespace

int main(int argc, char *argv[]) {
  return RunUi2Product(argc, argv);
}


extern "C" {
  int main(int argc, char *argv[]);
  void app_main(void) {
    (void)main(0, NULL);
  }
}
