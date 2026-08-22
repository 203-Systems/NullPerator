/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Browser system services for PicoTracker.
 */

#ifndef PICOTRACKER_WASM_SYSTEM_H
#define PICOTRACKER_WASM_SYSTEM_H

#include "System/System/System.h"

#include <atomic>
#include <cstdint>
#include <functional>

class WasmClock {
public:
  using NowFunction = std::function<double()>;

  WasmClock();
  explicit WasmClock(NowFunction now);

  std::uint32_t Micros();
  std::uint32_t Millis();

private:
  std::uint64_t MonotonicMicros();

  NowFunction now_;
  std::atomic<std::uint64_t> lastMicros_{0};
};

class WasmSystem final : public System {
public:
  static bool InstallPlatformServices();
  static void ShutdownPlatformServices();

  unsigned long GetClock() override;
  void GetBatteryState(BatteryState &state) override;
  void SetDisplayBrightness(unsigned char value) override;
  void PostQuitMessage() override;
  unsigned int GetMemoryUsage() override;
  void PowerDown() override;
  void SystemPutChar(int c) override;
  void SystemBootloader() override;
  void SystemReboot() override;
  std::uint32_t GetRandomNumber() override;
  std::uint32_t Micros() override;
  std::uint32_t Millis() override;

private:
  WasmClock clock_;
};

#endif
