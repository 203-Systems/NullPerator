/* SPDX-License-Identifier: BSD-3-Clause */

#include "platform.h"

#include "System/System/System.h"

#include <mutex>

namespace {
class IOSMutex final : public SysMutex {
public:
  bool Lock() override {
    mutex_.lock();
    return true;
  }
  void Unlock() override { mutex_.unlock(); }

private:
  std::recursive_mutex mutex_;
};
} // namespace

void platform_init() {}

SysMutex *platform_mutex() {
  static IOSMutex mutex;
  return &mutex;
}

std::uint32_t millis() {
  System *system = System::GetInstance();
  return system == nullptr ? 0U : system->Millis();
}

std::uint32_t micros() {
  System *system = System::GetInstance();
  return system == nullptr ? 0U : system->Micros();
}

void platform_brightness(std::uint8_t) {}
