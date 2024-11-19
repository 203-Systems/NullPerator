/*
 *  SysMutex.cpp
 *  lgpt
 *
 *  Created by Marc Nostromo on 10/03/12.
 *  Copyright 2012 __MyCompanyName__. All rights reserved.
 *
 */

#include "SysMutex.h"

SysMutex::SysMutex(): mutex_(0) {}

SysMutex::~SysMutex() {
#ifndef PICOBUILD
  if (mutex_) {
    SDL_DestroyMutex(mutex_);
    mutex_ = NULL;
  }
#endif
}

bool SysMutex::Lock() {
#ifndef PICOBUILD
  if (!mutex_) {
    mutex_ = SDL_CreateMutex();
  }
  if (mutex_) {
    SDL_LockMutex(mutex_);
    return true;
  }
#elif defined(ESP_PLATFORM)
  if (!mutex_) {
    mutex_ = xSemaphoreCreateMutex();
  }
  if (mutex_) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    return true;
  }
#elif defined(PICO_PLATFORM)
  if (!mutex_) {
    mutex_init(mutex_);
  }
  if (mutex_) {
    mutex_enter_blocking(mutex_);
    return true;
  }
#endif
  return false;
}

void SysMutex::Unlock() {
  if (mutex_) {
#ifndef PICOBUILD
    SDL_UnlockMutex(mutex_);
#elif defined(ESP_PLATFORM)
    xSemaphoreGive(mutex_);
#elif defined(PICO_PLATFORM)
    mutex_exit(mutex_);
#endif
  }
}

SysMutexLocker::SysMutexLocker(SysMutex &mutex) : mutex_(&mutex) {
  mutex_->Lock();
}

SysMutexLocker::~SysMutexLocker() { mutex_->Unlock(); }
