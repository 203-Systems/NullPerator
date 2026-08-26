/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Adapters/node/system/Ui2System.h"

#include "Adapters/node/audio/Audio.h"
#include "Adapters/node/filesystem/FileSystem.h"
#include "Adapters/node/hal/nullperator/power/power.h"
#include "Adapters/node/midi/MidiService.h"
#include "Adapters/node/platform/platform.h"
#include "Adapters/node/system/SamplePool.h"
#include "Adapters/node/timer/Timer.h"
#include "Application/Instruments/SamplePool.h"
#include "Services/Audio/Audio.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Timer/Timer.h"

#ifdef DUMMY_MIDI
#include "Adapters/Dummy/Midi/DummyMidi.h"
#endif

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <cmath>
#include <cstdio>
#include <malloc.h>
#include <memory>

std::atomic<bool> NodeUi2System::quitRequested_{false};
std::uint8_t NodeUi2System::requestedBrightness_ = 0U;
bool NodeUi2System::displayRevealed_ = false;
bool NodeUi2System::booted_ = false;

bool NodeUi2System::Boot(int, char **) {
  if (booted_)
    return true;

  alignas(NodeUi2System) static std::byte systemStorage[sizeof(NodeUi2System)];
  System::Install(std::construct_at(
      reinterpret_cast<NodeUi2System *>(systemStorage)));

  alignas(NodeTimerService)
      static std::byte timerStorage[sizeof(NodeTimerService)];
  TimerService::GetInstance()->Install(std::construct_at(
      reinterpret_cast<NodeTimerService *>(timerStorage)));

  alignas(NodeFileSystem)
      static std::byte fileSystemStorage[sizeof(NodeFileSystem)];
  FileSystem::Install(std::construct_at(
      reinterpret_cast<NodeFileSystem *>(fileSystemStorage)));
  if (!FileSystem::GetInstance()->chdir("/"))
    Trace::Error("NODE_UI2", "SD card root is unavailable");

  // Config observes MIDI settings while it is constructed, so the service
  // installation order deliberately remains identical to the proven legacy
  // Node boot path: MIDI before Audio.
#ifdef DUMMY_MIDI
  alignas(DummyMidi) static std::byte midiStorage[sizeof(DummyMidi)];
  MidiService::Install(
      std::construct_at(reinterpret_cast<DummyMidi *>(midiStorage)));
#else
  alignas(NodeMidiService)
      static std::byte midiStorage[sizeof(NodeMidiService)];
  MidiService::Install(std::construct_at(
      reinterpret_cast<NodeMidiService *>(midiStorage)));
#endif

  AudioSettings audioSettings;
  audioSettings.bufferSize_ = 1024;
  audioSettings.preBufferCount_ = 8;
  alignas(NodeAudio) static std::byte audioStorage[sizeof(NodeAudio)];
  Audio::Install(std::construct_at(reinterpret_cast<NodeAudio *>(audioStorage),
                                   audioSettings));
  // From this point Shutdown() is safe and must run on every later failure.
  // Audio::Init itself remains owned by Ui2TrackerApplication after project
  // persistence and all platform services are available.
  booted_ = true;

  void *samplePoolStorage = nullptr;
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0U) {
    samplePoolStorage = heap_caps_aligned_alloc(
        alignof(NodeSamplePool), sizeof(NodeSamplePool),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (samplePoolStorage == nullptr) {
    samplePoolStorage = heap_caps_aligned_alloc(
        alignof(NodeSamplePool), sizeof(NodeSamplePool),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (samplePoolStorage == nullptr) {
    Trace::Error("NODE_UI2", "Unable to allocate SamplePool service");
    Shutdown();
    return false;
  }
  SamplePool::Install(
      std::construct_at(static_cast<NodeSamplePool *>(samplePoolStorage)));

  requestedBrightness_ = 0U;
  displayRevealed_ = false;
  quitRequested_.store(false, std::memory_order_release);
  return true;
}

void NodeUi2System::Shutdown() {
  if (!booted_)
    return;

  // The application task has already closed its project, Player, previews and
  // MIDI ownership before this service-level shutdown is entered.
  if (Audio *audio = Audio::GetInstance())
    audio->Close();
  platform_brightness(0U);
  displayRevealed_ = false;
  booted_ = false;
}

void NodeUi2System::RevealDisplay() {
  if (displayRevealed_)
    return;
  displayRevealed_ = true;
  platform_brightness(requestedBrightness_);
}

bool NodeUi2System::QuitRequested() {
  return quitRequested_.load(std::memory_order_acquire);
}

unsigned long NodeUi2System::GetClock() { return Millis(); }

void NodeUi2System::GetBatteryState(BatteryState &state) {
  const float voltage = NullperatorHAL::Power::GetBatteryVoltage();
  if (voltage <= 0.0F) {
    state = {.percentage = 0U,
             .voltage_mv = 0U,
             .temperature_c = 0,
             .charging = false,
             .error = true};
    return;
  }

  state = {
      .percentage = NullperatorHAL::Power::GetBatteryPercentage(),
      .voltage_mv =
          static_cast<std::uint16_t>(std::lround(voltage * 1000.0F)),
      .temperature_c = 0,
      .charging = NullperatorHAL::Power::IsCharging(),
      .error = false,
  };
}

void NodeUi2System::SetDisplayBrightness(unsigned char value) {
  requestedBrightness_ = value;
  if (displayRevealed_)
    platform_brightness(value);
}

void NodeUi2System::PostQuitMessage() {
  quitRequested_.store(true, std::memory_order_release);
}

unsigned int NodeUi2System::GetMemoryUsage() {
  const struct mallinfo info = mallinfo();
  return static_cast<unsigned int>(info.uordblks);
}

void NodeUi2System::PowerDown() { enter_sleep(); }

void NodeUi2System::SystemBootloader() { SystemReboot(); }

void NodeUi2System::SystemReboot() { esp_restart(); }

void NodeUi2System::SystemPutChar(int value) { putchar(value); }

std::uint32_t NodeUi2System::GetRandomNumber() { return esp_random(); }

std::uint32_t NodeUi2System::Micros() {
  return static_cast<std::uint32_t>(esp_timer_get_time());
}

std::uint32_t NodeUi2System::Millis() { return Micros() / 1000U; }
