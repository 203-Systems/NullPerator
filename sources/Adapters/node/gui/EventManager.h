#ifndef _EVENTMANAGER_H_
#define _EVENTMANAGER_H_

#include "Foundation/T_Singleton.h"
#include "SerialDebugUI.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"
#include <string>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "Adapters/node/system/EventQueue.h"

class NodeEventManager : public T_Singleton<NodeEventManager>,
                                public EventManager {
public:
  NodeEventManager();
  ~NodeEventManager();
  virtual bool Init();
  virtual int MainLoop();
  virtual void PostQuitMessage();
  virtual int GetKeyCode(const char *name);

  // Thread-safe event posting (task context).
  static void PostEvent(NodeEventType type);

protected:
  static void ProcessInputEvent(void *);
  static void ProcessEvent(void *);
  static void USBDevice(void *);
  static void ProcessSerialInputEvent(void *);

private:
  static TimerHandle_t timer_;
  static QueueHandle_t eventQueue_;

  static bool finished_;
  static bool redrawing_;
  static uint16_t buttonMask_;
  static unsigned int keyRepeat_;
  static unsigned int keyDelay_;
  static unsigned int keyKill_;
  static bool isRepeating_;
  static unsigned long time_;

  static SerialDebugUI serialDebugUI_;
};
#endif
