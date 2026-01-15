/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025
 *
 * This file is part of the node firmware
 */

#ifndef _NODE_MUTEX_H_
#define _NODE_MUTEX_H_

#include "System/Process/SysMutex.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class NodeMutex : public SysMutex {
public:
  NodeMutex();
  virtual ~NodeMutex(){};
  virtual bool Lock() override;
  virtual void Unlock() override;

private:
  SemaphoreHandle_t mutex_;
};

#endif
