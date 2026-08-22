/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Adapters/wasm/gui/GUIFactory.h"

#include "Adapters/wasm/gui/WasmEventManager.h"
#include "Adapters/wasm/gui/WasmGUIWindowImp.h"

#include <new>

I_GUIWindowImp &GUIFactory::CreateWindowImp(GUICreateWindowParams &params) {
  alignas(WasmGUIWindowImp)
      static unsigned char storage[sizeof(WasmGUIWindowImp)];
  return *(new (storage) WasmGUIWindowImp(params));
}

EventManager *GUIFactory::GetEventManager() {
  return WasmEventManager::GetInstance();
}
