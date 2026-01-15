#ifndef _NODESYSTEM_H_
#define _NODESYSTEM_H_

#include <map>

#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

#define USEREVENT_TIMER 0
#define USEREVENT_EXPOSE 1

class NodeSystem : public System {
public:
  static void Boot(int argc, char **argv);
  static void Shutdown();
  static int MainLoop();

public: // System implementation
  virtual unsigned long GetClock() override;
  virtual void GetBatteryState(BatteryState &state) override;
  virtual void SetDisplayBrightness(unsigned char value) override;
  virtual void PostQuitMessage() override;
  virtual unsigned int GetMemoryUsage() override;
  virtual void PowerDown() override;
  virtual void SystemBootloader() override;
  virtual void SystemReboot() override;
  virtual void SystemPutChar(int c) override;
  virtual uint32_t GetRandomNumber() override;
  virtual uint32_t Micros() override;
  virtual uint32_t Millis() override;

  virtual void Sleep(int millisec);

private:
  static bool invert_;
  static EventManager *eventManager_;
};
#endif
