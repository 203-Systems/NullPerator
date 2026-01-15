/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025
 *
 * This file is part of the esp32 firmware
 */

#include "Mutex.h"

#include "System/Console/Trace.h"

NodeMutex::NodeMutex() : mutex_(xSemaphoreCreateRecursiveMutex()) {}

bool NodeMutex::Lock() {
  if (xSemaphoreTakeRecursive(mutex_, portMAX_DELAY)) {
    return true;
  }
  Trace::Error("Mutex take failed");
  return false;
}

void NodeMutex::Unlock() {
  if (!xSemaphoreGiveRecursive(mutex_)) {
    Trace::Error("Mutex release failed");
  }
}
