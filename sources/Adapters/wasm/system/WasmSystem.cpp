/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "WasmSystem.h"

#ifndef HOST_TEST
#include "Adapters/wasm/audio/WasmAudio.h"
#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Adapters/wasm/logging/WasmTrace.h"
#include "Adapters/wasm/midi/WasmMidiService.h"
#include "Adapters/wasm/platform/wasm_bridge.h"
#include "Adapters/wasm/process/WasmProcess.h"
#include "Adapters/wasm/system/WasmSamplePool.h"
#include "Adapters/wasm/timer/WasmTimer.h"
#include "Services/Audio/Audio.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Timer/Timer.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <random>
#include <utility>

namespace {
double DefaultNowMilliseconds() {
#ifdef __EMSCRIPTEN__
  return emscripten_get_now();
#else
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(
             Clock::now().time_since_epoch())
      .count();
#endif
}
} // namespace

WasmClock::WasmClock() : WasmClock(DefaultNowMilliseconds) {}

WasmClock::WasmClock(NowFunction now) : now_(std::move(now)) {}

std::uint64_t WasmClock::MonotonicMicros() {
  double milliseconds = now_ == nullptr ? 0.0 : now_();
  if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
    milliseconds = 0.0;
  }
  const double micros = milliseconds * 1000.0;
  const auto sampled = static_cast<std::uint64_t>(std::min(
      micros, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));

  std::uint64_t previous = lastMicros_.load(std::memory_order_relaxed);
  while (sampled > previous &&
         !lastMicros_.compare_exchange_weak(previous, sampled,
                                            std::memory_order_relaxed)) {
  }
  return std::max(sampled, previous);
}

std::uint32_t WasmClock::Micros() {
  return static_cast<std::uint32_t>(MonotonicMicros());
}

std::uint32_t WasmClock::Millis() {
  return static_cast<std::uint32_t>(MonotonicMicros() / 1000U);
}

#ifndef HOST_TEST
bool WasmSystem::InstallPlatformServices() {
  alignas(WasmSystem) static unsigned char systemStorage[sizeof(WasmSystem)];
  System::Install(new (systemStorage) WasmSystem());

  // Construct before any platform service can emit Trace output. The sink is
  // fixed-storage and remains valid until the WASM module is terminated.
  static WasmTrace trace;

  alignas(WasmTimerService)
      static unsigned char timerStorage[sizeof(WasmTimerService)];
  TimerService::Install(new (timerStorage) WasmTimerService());

  alignas(WasmFileSystem)
      static unsigned char filesystemStorage[sizeof(WasmFileSystem)];
  FileSystem::Install(new (filesystemStorage) WasmFileSystem());
  if (!FileSystem::GetInstance()->chdir("/")) {
    Trace::Error("WASM_SYSTEM", "failed to enter MEMFS root /data");
    return false;
  }

  alignas(WasmMidiService)
      static unsigned char midiStorage[sizeof(WasmMidiService)];
  MidiService::Install(new (midiStorage) WasmMidiService());

  AudioSettings settings{};
  settings.audioAPI_ = "wasm-audio-worklet";
  settings.audioDevice_ = "browser-default";
  settings.bufferSize_ = 1024;
  settings.preBufferCount_ = 4;
  alignas(WasmAudio) static unsigned char audioStorage[sizeof(WasmAudio)];
  Audio::Install(new (audioStorage) WasmAudio(settings));

  alignas(WasmSamplePool)
      static unsigned char samplePoolStorage[sizeof(WasmSamplePool)];
  SamplePool::Install(new (samplePoolStorage) WasmSamplePool());
  return true;
}

void WasmSystem::ShutdownPlatformServices() {
  Audio *audio = Audio::GetInstance();
  if (audio != nullptr) {
    audio->Close();
  }
  MidiService *midi = MidiService::GetInstance();
  if (midi != nullptr) {
    midi->Close();
  }
  if (WasmTrace *trace = WasmTrace::Instance()) trace->FlushLine();
}

unsigned long WasmSystem::GetClock() { return Millis(); }

void WasmSystem::GetBatteryState(BatteryState &state) {
  state.percentage = 0;
  state.voltage_mv = 0;
  state.temperature_c = 0;
  state.charging = false;
  state.error = true;
}

void WasmSystem::SetDisplayBrightness(unsigned char value) {
  platform_brightness(value);
}

void WasmSystem::PostQuitMessage() { PicoTracker_Wasm_RequestShutdown(); }

unsigned int WasmSystem::GetMemoryUsage() { return 0; }

void WasmSystem::PowerDown() { (void)WasmProcess::PowerDown(); }

void WasmSystem::SystemPutChar(int c) {
  if (WasmTrace *trace = WasmTrace::Instance()) trace->PutChar(c);
  else std::putchar(c);
}

void WasmSystem::SystemBootloader() {
  (void)WasmProcess::EnterBootloader();
}

void WasmSystem::SystemReboot() { (void)WasmProcess::Reboot(); }

std::uint32_t WasmSystem::GetRandomNumber() {
  static thread_local std::mt19937 generator(std::random_device{}());
  return generator();
}

std::uint32_t WasmSystem::Micros() { return clock_.Micros(); }

std::uint32_t WasmSystem::Millis() { return clock_.Millis(); }
#endif
