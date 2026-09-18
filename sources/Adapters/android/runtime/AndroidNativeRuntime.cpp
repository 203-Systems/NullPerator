/* SPDX-License-Identifier: BSD-3-Clause */

#include "AndroidNativeRuntime.h"

#include "Adapters/common/midi/QueuedMidiService.h"
#include "Adapters/common/system/HeapSamplePool.h"
#include "Adapters/android/audio/AndroidAudio.h"
#include "Adapters/android/audio/AndroidAudioDriver.h"
#include "Adapters/android/gui/AndroidUiPresenter.h"
#include "Adapters/android/system/AndroidSystem.h"
#include "Adapters/android/timer/AndroidTimer.h"
#include "Adapters/posix/filesystem/PosixFileSystem.h"
#include "Application/Input/TrackerInput.h"
#include "Application/UI2/Ui2TrackerApplication.h"
#include "Services/Audio/Audio.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Timer/Timer.h"

#include "Application/Audio/RecordingPlatform.h"
#include <cstring>
#include <new>

namespace {
#ifndef NULLPERATOR_BUILD_HASH
#define NULLPERATOR_BUILD_HASH "unknown"
#endif
#ifndef NULLPERATOR_BUILD_TIME
#define NULLPERATOR_BUILD_TIME "unknown"
#endif

AndroidSystem systemService;
AndroidTimerService timerService;
alignas(
    PosixFileSystem) unsigned char filesystemStorage[sizeof(PosixFileSystem)];
PosixFileSystem *filesystemService = nullptr;
alignas(QueuedMidiService) unsigned char midiStorage[sizeof(QueuedMidiService)];
QueuedMidiService *midiService = nullptr;
AudioSettings audioSettings{};
alignas(AndroidAudio) unsigned char audioStorage[sizeof(AndroidAudio)];
AndroidAudio *audioService = nullptr;
alignas(HeapSamplePool) unsigned char samplePoolStorage[sizeof(HeapSamplePool)];
HeapSamplePool *samplePoolService = nullptr;
} // namespace

AndroidNativeRuntime::AndroidNativeRuntime(std::string documentsPath)
    : documentsPath_(std::move(documentsPath)) {}

AndroidNativeRuntime::~AndroidNativeRuntime() { Shutdown(); }

bool AndroidNativeRuntime::Init() {
  if (initialized_)
    return true;
  System::Install(&systemService);
  TimerService::Install(&timerService);
  if (filesystemService == nullptr) {
    filesystemService = new (filesystemStorage) PosixFileSystem(documentsPath_);
  }
  FileSystem::Install(filesystemService);
  if (!filesystemService->chdir("/"))
    return false;
  if (midiService == nullptr)
    midiService = new (midiStorage) QueuedMidiService();
  MidiService::Install(midiService);
  audioSettings.audioAPI_ = "android-aaudio";
  audioSettings.audioDevice_ = "system-output";
  audioSettings.bufferSize_ = 512;
  audioSettings.preBufferCount_ = 4;
  if (audioService == nullptr)
    audioService = new (audioStorage) AndroidAudio(audioSettings);
  Audio::Install(audioService);
  if (samplePoolService == nullptr)
    samplePoolService = new (samplePoolStorage) HeapSamplePool();
  SamplePool::Install(samplePoolService);
  servicesInstalled_ = true;

  presenter_ = std::make_unique<AndroidUiPresenter>();
  application_ = std::make_unique<ui2::Ui2TrackerApplication>(*presenter_);
  if (!application_->Init()) {
    application_.reset();
    presenter_.reset();
    return false;
  }
  initialized_ = true;
  (void)application_->Present();
  return true;
}

void AndroidNativeRuntime::Suspend(bool suspended) {
  ReleaseAllActions();
  if (suspended && application_) {
    application_->SuspendHost();
  }
  if (auto *driver = AndroidAudio::Driver()) driver->SetSuspended(suspended);
}

void AndroidNativeRuntime::Shutdown() {
  if (application_ != nullptr)
    application_->Shutdown();
  application_.reset();
  presenter_.reset();
  if (servicesInstalled_) {
    if (audioService != nullptr)
      audioService->Close();
    if (midiService != nullptr)
      midiService->Close();
  }
  heldMask_ = 0U;
  initialized_ = false;
}

void AndroidNativeRuntime::SetAction(std::uint8_t action, bool pressed,
                                 bool repeated) {
  if (!initialized_ ||
      action >= static_cast<std::uint8_t>(TrackerAction::Count))
    return;
  const auto trackerAction = static_cast<TrackerAction>(action);
  const std::uint16_t bit = TrackerActionBit(trackerAction);
  const bool wasPressed = (heldMask_ & bit) != 0U;
  if (pressed)
    heldMask_ |= bit;
  else
    heldMask_ &= static_cast<std::uint16_t>(~bit);
  if (pressed && repeated && wasPressed) {
    application_->DispatchTrackerAction(trackerAction, false);
    application_->DispatchTrackerAction(trackerAction, true);
  } else if (pressed != wasPressed) {
    application_->DispatchTrackerAction(trackerAction, pressed);
  }
  (void)application_->Present();
}

void AndroidNativeRuntime::ReleaseAllActions() {
  if (!initialized_)
    return;
  for (std::uint8_t action = 0U;
       action < static_cast<std::uint8_t>(TrackerAction::Count); ++action) {
    const auto trackerAction = static_cast<TrackerAction>(action);
    if ((heldMask_ & TrackerActionBit(trackerAction)) != 0U)
      application_->DispatchTrackerAction(trackerAction, false);
  }
  heldMask_ = 0U;
  (void)application_->Present();
}

void AndroidNativeRuntime::Tick() {
  if (!initialized_)
    return;
  if (AndroidAudioDriver *driver = AndroidAudio::Driver())
    driver->PumpProducer();
  if (midiService != nullptr)
    midiService->Poll();
  application_->Tick(systemService.Millis());
  (void)application_->Present();
}

bool AndroidNativeRuntime::DrainFrame(std::uint32_t afterSequence,
                                  AndroidUiFramePacket &packet) {
  return presenter_ != nullptr && presenter_->DrainFrame(afterSequence, packet);
}

void AndroidNativeRuntime::SetBattery(std::uint8_t percentage, bool charging,
                                  bool available) {
  AndroidSystem::SetBatteryState(percentage, charging, available);
}

bool AndroidNativeRuntime::SubmitMidi(const std::uint8_t *bytes, std::size_t size,
                                  double timestampMilliseconds) {
  return initialized_ && midiService != nullptr &&
         midiService->SubmitInput(bytes, size, timestampMilliseconds);
}

AndroidNativeRuntime::MidiDrain AndroidNativeRuntime::DrainMidi() {
  MidiDrain result;
  if (!initialized_ || midiService == nullptr)
    return result;
  const auto pointer = midiService->DrainOutput();
  if (pointer == 0U)
    return result;
  const auto *data = reinterpret_cast<const std::uint8_t *>(pointer);
  auto load32 = [data](std::size_t offset) {
    std::uint32_t value = 0U;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
  };
  const std::uint32_t version = load32(0U);
  const std::uint32_t headerBytes = load32(4U);
  const std::uint32_t recordBytes = load32(8U);
  const std::uint32_t count = load32(12U);
  if (version != 1U || headerBytes != QueuedMidiService::DrainHeaderBytes ||
      recordBytes != QueuedMidiService::DrainRecordBytes ||
      count > QueuedMidiService::DrainCapacity) {
    return result;
  }
  result.droppedNormal = load32(16U);
  result.droppedRealtime = load32(20U);
  result.packets.reserve(count);
  for (std::uint32_t index = 0U; index < count; ++index) {
    const auto *record = data + headerBytes + index * recordBytes;
    MidiPacket packet;
    std::memcpy(&packet.sequence, record, sizeof(packet.sequence));
    packet.length = record[16U];
    if (packet.length == 0U || packet.length > packet.bytes.size())
      continue;
    std::memcpy(packet.bytes.data(), record + 17U, packet.length);
    result.packets.push_back(packet);
  }
  return result;
}

void AndroidNativeRuntime::DisconnectMidi(std::uint32_t directions) {
  if (midiService != nullptr)
    midiService->Disconnect(directions);
}

void AndroidNativeRuntime::SetMidiOutputConnected(bool connected) {
  if (midiService != nullptr)
    midiService->SetOutputConnected(connected);
}

const char *AndroidNativeRuntime::BuildHash() noexcept {
  return NULLPERATOR_BUILD_HASH;
}

const char *AndroidNativeRuntime::BuildTime() noexcept {
  return NULLPERATOR_BUILD_TIME;
}
