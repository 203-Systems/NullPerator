/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "Foundation/T_Singleton.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

#include <atomic>

class WasmEventManager final : public T_Singleton<WasmEventManager>,
                               public EventManager {
public:
  bool Init() override;
  int MainLoop() override;
  void PostQuitMessage() override;
  int GetKeyCode(const char *name) override;

private:
  static void RunFrame(void *context);
  void PumpFrame();
  void StopRuntime();

  std::atomic<bool> finished_{false};
  bool runtimeStopped_ = false;
  double nextTick_ = 0.0;
};
