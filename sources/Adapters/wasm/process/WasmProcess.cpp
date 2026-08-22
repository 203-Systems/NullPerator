/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmProcess.h"

#include "Adapters/wasm/mutex/WasmMutex.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

#include <array>
#include <atomic>

namespace {
enum class UnsupportedOperation : std::size_t {
  PowerDown,
  Bootloader,
  Reboot,
  Count,
};

std::array<std::atomic<bool>,
           static_cast<std::size_t>(UnsupportedOperation::Count)>
    logged{};

bool ReportUnsupported(UnsupportedOperation operation, const char *name) {
  bool expected = false;
  if (logged[static_cast<std::size_t>(operation)].compare_exchange_strong(
          expected, true, std::memory_order_relaxed)) {
    Trace::Error("WASM_PROCESS", "unsupported operation: %s", name);
  }
  return false;
}
} // namespace

bool WasmProcess::PowerDown() {
  return ReportUnsupported(UnsupportedOperation::PowerDown, "power-down");
}

bool WasmProcess::EnterBootloader() {
  return ReportUnsupported(UnsupportedOperation::Bootloader, "bootloader");
}

bool WasmProcess::Reboot() {
  return ReportUnsupported(UnsupportedOperation::Reboot, "reboot");
}

void platform_init() {}

SysMutex *platform_mutex() {
  static WasmMutex mutex;
  return &mutex;
}

std::uint32_t millis() {
  System *system = System::GetInstance();
  return system == nullptr ? 0 : system->Millis();
}

std::uint32_t micros() {
  System *system = System::GetInstance();
  return system == nullptr ? 0 : system->Micros();
}

void platform_brightness(std::uint8_t) {}
