/* SPDX-License-Identifier: BSD-3-Clause */

#include "AndroidSystem.h"

#include <algorithm>
#include <cstdio>
#include <random>

namespace {
constexpr std::uint32_t kCharging = 1U << 8U;
constexpr std::uint32_t kAvailable = 1U << 9U;
std::atomic<std::uint32_t> battery{0U};
} // namespace

unsigned long AndroidSystem::GetClock() { return Millis(); }

void AndroidSystem::GetBatteryState(BatteryState &state) {
  const std::uint32_t value = battery.load(std::memory_order_acquire);
  state.percentage = static_cast<std::uint8_t>(value & 0x7FU);
  state.voltage_mv = 0U;
  state.temperature_c = 0;
  state.charging = (value & kCharging) != 0U;
  state.error = (value & kAvailable) == 0U;
}

void AndroidSystem::SetDisplayBrightness(unsigned char) {}
void AndroidSystem::PostQuitMessage() {}
unsigned int AndroidSystem::GetMemoryUsage() { return 0U; }
void AndroidSystem::PowerDown() {}
void AndroidSystem::SystemPutChar(int c) { std::putchar(c); }
void AndroidSystem::SystemBootloader() {}
void AndroidSystem::SystemReboot() {}

std::uint32_t AndroidSystem::GetRandomNumber() {
  static thread_local std::mt19937 generator(std::random_device{}());
  return generator();
}

std::uint32_t AndroidSystem::Micros() {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                            started_)
          .count());
}

std::uint32_t AndroidSystem::Millis() { return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_).count()); }

void AndroidSystem::SetBatteryState(std::uint8_t percentage, bool charging,
                                bool available) noexcept {
  battery.store(std::min<std::uint32_t>(percentage, 100U) |
                    (charging ? kCharging : 0U) | (available ? kAvailable : 0U),
                std::memory_order_release);
}

extern "C" bool NullPeratorAndroidRequestSampleImport(const char *projectName);
SampleImportResult NullPeratorAndroidPollSampleImport();
bool AndroidSystem::RequestSampleImport(const char *projectName) {
  return NullPeratorAndroidRequestSampleImport(projectName);
}
SampleImportResult AndroidSystem::PollSampleImport() {
  return NullPeratorAndroidPollSampleImport();
}
