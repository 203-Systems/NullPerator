#include "EventManager.h"
#include "Adapters/node/platform/platform.h"
#include "Adapters/node/system/input.h"
#include "Application/Application.h"
#include "GUIWindowImp.h"
#include "esp_log.h"
#include "tusb.h"

bool NodeEventManager::finished_ = false;
bool NodeEventManager::redrawing_ = false;
uint16_t NodeEventManager::buttonMask_ = 0;

bool NodeEventManager::isRepeating_ = false;
unsigned long NodeEventManager::time_ = 0;
unsigned int NodeEventManager::keyRepeat_ = 25;
unsigned int NodeEventManager::keyDelay_ = 500;
unsigned int NodeEventManager::keyKill_ = 5;
TimerHandle_t  NodeEventManager::timer_ = NULL;
QueueHandle_t NodeEventManager::eventQueue_ = NULL;
SerialDebugUI NodeEventManager::serialDebugUI_ = SerialDebugUI();

#ifdef SERIAL_REPL
#define INPUT_BUFFER_SIZE 80
char inBuffer[INPUT_BUFFER_SIZE];
#endif

static void timerHandler(TimerHandle_t xTimer) {
    (void)xTimer;
    NodeEventManager::PostEvent(CLOCK);
}

NodeEventManager::NodeEventManager() {}

NodeEventManager::~NodeEventManager() {}

void NodeEventManager::PostEvent(NodeEventType type) {
  if (!eventQueue_) {
    return;
  }
  const NodeEvent ev(type);
  (void)xQueueSend(eventQueue_, &ev, 0);
}

bool NodeEventManager::Init() {
  EventManager::Init();
  return true;
}

int NodeEventManager::MainLoop() {
  // Mirror ADV structure: a FreeRTOS queue + tasks for input, event dispatch,
  // and USB. ESP-IDF has already started the scheduler, so we only create tasks
  // and then block forever.
  static constexpr uint32_t kEventQueueLength = 32;
  static constexpr uint32_t kInputTaskStackBytes = 4096;
  static constexpr uint32_t kEventTaskStackBytes = 8192;
  static constexpr uint32_t kSerialTaskStackBytes = 2048;
  static constexpr uint32_t kUsbTaskStackBytes = 2048;
  eventQueue_ = xQueueCreate(kEventQueueLength, sizeof(NodeEvent));
  if (!eventQueue_) {
    ESP_LOGE("NODE", "Failed to create event queue");
    return 0;
  }

  // Create periodic timer for CLOCK (same period as ADV timerHandler).
  timer_ = xTimerCreate("NodeTimer", pdMS_TO_TICKS(PICO_CLOCK_INTERVAL), pdTRUE,
                        NULL, timerHandler);
  if (!timer_) {
    ESP_LOGE("NODE", "Failed to create UI timer");
    return 0;
  }
  (void)xTimerStart(timer_, pdMS_TO_TICKS(100));

  // Tasks: keep names and roles aligned with ADV.
  (void)xTaskCreatePinnedToCore(NodeEventManager::ProcessInputEvent, "InEvent",
                                kInputTaskStackBytes, NULL,
                                tskIDLE_PRIORITY + 2, NULL, 0);
#ifdef SERIAL_REPL
  (void)xTaskCreatePinnedToCore(NodeEventManager::ProcessSerialInputEvent,
                                "SerialInEvent", kSerialTaskStackBytes, NULL,
                                tskIDLE_PRIORITY + 2, NULL, 0);
#endif
  (void)xTaskCreatePinnedToCore(NodeEventManager::ProcessEvent, "ProcEvent",
                                kEventTaskStackBytes, NULL,
                                tskIDLE_PRIORITY + 1, NULL, 0);
  (void)xTaskCreatePinnedToCore(NodeEventManager::USBDevice, "USB Device",
                                kUsbTaskStackBytes, NULL,
                                tskIDLE_PRIORITY + 2, NULL, 0);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return 0;
}

void NodeEventManager::PostQuitMessage() {
  // Trace:Log("EVENT", "quit");
  finished_ = true;
}

int NodeEventManager::GetKeyCode(const char *name) { return -1; }

void NodeEventManager::ProcessEvent(void *) {
  NodeEvent event(NodeEventType::LAST);
  for (;;) {
    if (xQueueReceive(eventQueue_, &event, portMAX_DELAY) == pdTRUE) {
      redrawing_ = true;
      NodeGUIWindowImp::ProcessEvent(event);
      redrawing_ = false;
    }
  }
}

void NodeEventManager::USBDevice(void *) {
  for (;;) {
    tud_task();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void NodeEventManager::ProcessInputEvent(void *) {
  for (;;) {
    if (finished_) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    uint16_t newMask = 0;
    uint16_t sendMask = 0;

    if (redrawing_) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    bool gotEvent = false;

    // Get current mask
    newMask = scanKeys();

    // compute mask to send
    sendMask = (newMask ^ buttonMask_) |
               (newMask & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN));
    unsigned long now = millis();
    // see if we're repeating
    if (newMask == buttonMask_) {
      if ((isRepeating_) && ((now - time_) > keyRepeat_)) {
        gotEvent = (sendMask != 0);
      }
      if ((!isRepeating_) && ((now - time_) > keyDelay_)) {
        gotEvent = (sendMask != 0);
        if (gotEvent) {
          isRepeating_ = true;
        }
      }
    } else {
      if ((now - time_) > keyKill_) {
        gotEvent = (sendMask != 0);
        if (gotEvent) {
          isRepeating_ = false;
        }
      }
    }
    if (gotEvent) {
      time_ = now; // Get time here so delay is independant of processing speed

      NodeGUIWindowImp::ProcessButtonChange(sendMask, newMask);
      buttonMask_ = newMask;
      PostEvent(REDRAW);
    }

#ifdef SERIAL_REPL
    serialDebugUI_.readSerialIn(inBuffer, INPUT_BUFFER_SIZE);
#endif

    // Polling period: keep fairly responsive while remaining ADV-like (task-based).
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void NodeEventManager::ProcessSerialInputEvent(void *) {
  for (;;) {
#ifdef SERIAL_REPL
    serialDebugUI_.readSerialIn(inBuffer, INPUT_BUFFER_SIZE);
#endif
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
