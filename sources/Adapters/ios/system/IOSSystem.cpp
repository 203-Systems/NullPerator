/* SPDX-License-Identifier: BSD-3-Clause */

#include "IOSSystem.h"

#include <algorithm>
#include <cstdio>
#include <random>

namespace {
constexpr std::uint32_t kCharging = 1U << 8U;
constexpr std::uint32_t kAvailable = 1U << 9U;
std::atomic<std::uint32_t> battery{0U};
} // namespace

unsigned long IOSSystem::GetClock() { return Millis(); }

void IOSSystem::GetBatteryState(BatteryState &state) {
  const std::uint32_t value = battery.load(std::memory_order_acquire);
  state.percentage = static_cast<std::uint8_t>(value & 0x7FU);
  state.voltage_mv = 0U;
  state.temperature_c = 0;
  state.charging = (value & kCharging) != 0U;
  state.error = (value & kAvailable) == 0U;
}

void IOSSystem::SetDisplayBrightness(unsigned char) {}
void IOSSystem::PostQuitMessage() {}
unsigned int IOSSystem::GetMemoryUsage() { return 0U; }
void IOSSystem::PowerDown() {}
void IOSSystem::SystemPutChar(int c) { std::putchar(c); }
void IOSSystem::SystemBootloader() {}
void IOSSystem::SystemReboot() {}

std::uint32_t IOSSystem::GetRandomNumber() {
  static thread_local std::mt19937 generator(std::random_device{}());
  return generator();
}

std::uint32_t IOSSystem::Micros() {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_)
          .count());
}

std::uint32_t IOSSystem::Millis() { return Micros() / 1000U; }

void IOSSystem::SetBatteryState(std::uint8_t percentage, bool charging,
                                bool available) noexcept {
  battery.store(std::min<std::uint32_t>(percentage, 100U) |
                    (charging ? kCharging : 0U) |
                    (available ? kAvailable : 0U),
                std::memory_order_release);
}
