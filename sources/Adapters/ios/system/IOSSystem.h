/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "System/System/System.h"

#include <atomic>
#include <chrono>
#include <cstdint>

class IOSSystem final : public System {
public:
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

  static void SetBatteryState(std::uint8_t percentage, bool charging,
                              bool available) noexcept;

private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point started_ = Clock::now();
};
