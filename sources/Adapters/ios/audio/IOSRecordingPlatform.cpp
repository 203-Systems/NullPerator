/* SPDX-License-Identifier: BSD-3-Clause */

#include "Application/Audio/RecordingPlatform.h"

#include "Adapters/ios/audio/IOSAudio.h"
#include "Adapters/ios/audio/IOSAudioDriver.h"
#include "Services/Audio/WavHeader.h"
#include "System/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

extern "C" void NullPeratorIOSRequestRecordPermission();

namespace {

constexpr std::uint32_t kSampleRate = 44100U;
constexpr std::uint32_t kMaximumDurationMs = 30000U;
constexpr std::size_t kWriteChunkSamples = 4096U;

std::vector<std::int16_t> captureBuffer;
std::array<char, 512> recordingPath{};
std::thread savingThread;
std::atomic<bool> recording{false};
std::atomic<bool> saving{false};
std::atomic<bool> lastSavedAudio{false};
std::atomic<std::uint8_t> savingProgress{0U};
std::atomic<std::uint32_t> elapsedMs{0U};

IOSAudioDriver *Driver() noexcept { return IOSAudio::Driver(); }

void JoinCompletedSavingThread() {
  if (savingThread.joinable() && !saving.load(std::memory_order_acquire))
    savingThread.join();
}

bool PersistRecording(std::size_t frames) {
  if (frames == 0U || recordingPath[0] == '\0')
    return false;
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr)
    return false;
  FileHandle file = fileSystem->Open(recordingPath.data(), "w+b");
  if (!file)
    return false;
  const auto fail = [&] {
    file.reset();
    (void)fileSystem->DeleteFile(recordingPath.data());
    return false;
  };
  if (!WavHeaderWriter::WriteHeader(file.get(), kSampleRate, 1U, 16U))
    return fail();

  std::size_t written = 0U;
  while (written < frames) {
    const std::size_t chunk = std::min(kWriteChunkSamples, frames - written);
    if (file->Write(captureBuffer.data() + written, sizeof(std::int16_t),
                    static_cast<int>(chunk)) != static_cast<int>(chunk))
      return fail();
    written += chunk;
    savingProgress.store(static_cast<std::uint8_t>((written * 100U) / frames),
                         std::memory_order_release);
  }
  if (!file->Sync() ||
      !WavHeaderWriter::UpdateFileSize(file.get(),
                                       static_cast<std::uint32_t>(frames), 1U,
                                       sizeof(std::int16_t)) ||
      !file->Sync())
    return fail();
  return true;
}

void SaveCapturedFrames(std::size_t frames) {
  const bool result = PersistRecording(frames);
  lastSavedAudio.store(result, std::memory_order_release);
  savingProgress.store(result ? 100U : 0U, std::memory_order_release);
  saving.store(false, std::memory_order_release);
}

void FinishCaptureAndSave() {
  bool expected = true;
  if (!recording.compare_exchange_strong(expected, false,
                                         std::memory_order_acq_rel))
    return;
  IOSAudioDriver *driver = Driver();
  if (driver == nullptr) {
    lastSavedAudio.store(false, std::memory_order_release);
    return;
  }
  driver->EndInputCapture();
  const std::size_t frames = driver->CapturedInputFrames();
  elapsedMs.store(static_cast<std::uint32_t>((frames * 1000ULL) / kSampleRate),
                  std::memory_order_release);
  JoinCompletedSavingThread();
  saving.store(true, std::memory_order_release);
  savingProgress.store(0U, std::memory_order_release);
  try {
    savingThread = std::thread([frames] { SaveCapturedFrames(frames); });
  } catch (...) {
    SaveCapturedFrames(frames);
  }
}

void FinishFullCaptureIfNeeded() {
  IOSAudioDriver *driver = Driver();
  if (recording.load(std::memory_order_acquire) && driver != nullptr &&
      !driver->IsInputCapturing())
    FinishCaptureAndSave();
}

} // namespace

void SetInputSource(RecordSource) {}

bool IsRecordingInputSelectable() { return false; }

void SetLineInGain(std::uint8_t) {}

void SetMicGain(std::uint8_t) {}

bool IsRecordingAvailable() {
  IOSAudioDriver *driver = Driver();
  return driver != nullptr && driver->InputAvailable();
}

bool IsRecordingActive() {
  FinishFullCaptureIfNeeded();
  return recording.load(std::memory_order_acquire);
}

bool IsMonitoringActive() {
  IOSAudioDriver *driver = Driver();
  return driver != nullptr && driver->InputAvailable() &&
         driver->IsInputMonitoring();
}

bool IsSavingRecording() {
  FinishFullCaptureIfNeeded();
  return saving.load(std::memory_order_acquire);
}

void Record(void *) {}

bool StartRecording(const char *filename, std::uint8_t,
                    std::uint32_t milliseconds) {
  JoinCompletedSavingThread();
  IOSAudioDriver *driver = Driver();
  if (driver == nullptr || !driver->InputAvailable() || filename == nullptr ||
      filename[0] == '\0' || recording.load(std::memory_order_acquire) ||
      saving.load(std::memory_order_acquire))
    return false;

  const std::uint32_t durationMs =
      milliseconds == 0U ? kMaximumDurationMs
                         : std::min(milliseconds, kMaximumDurationMs);
  try {
    captureBuffer.assign(
        static_cast<std::size_t>((kSampleRate * durationMs) / 1000U), 0);
  } catch (...) {
    return false;
  }
  if (captureBuffer.empty())
    return false;
  std::snprintf(recordingPath.data(), recordingPath.size(), "%s", filename);
  lastSavedAudio.store(false, std::memory_order_release);
  savingProgress.store(0U, std::memory_order_release);
  elapsedMs.store(0U, std::memory_order_release);
  driver->SetInputMonitoring(false);
  if (!driver->BeginInputCapture(captureBuffer)) {
    captureBuffer.clear();
    return false;
  }
  recording.store(true, std::memory_order_release);
  return true;
}

void StopRecording() {
  RequestStopRecording();
  (void)WaitForRecordingStop(UINT32_MAX);
  FinishStopRecording();
}

void RequestStopRecording() { FinishCaptureAndSave(); }

bool WaitForRecordingStop(std::uint32_t timeoutMs) {
  RequestStopRecording();
  const auto started = std::chrono::steady_clock::now();
  while (saving.load(std::memory_order_acquire)) {
    if (timeoutMs != UINT32_MAX) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started);
      if (elapsed.count() >= timeoutMs)
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  JoinCompletedSavingThread();
  return true;
}

void FinishStopRecording() { JoinCompletedSavingThread(); }

void StartMonitoring() {
  if (recording.load(std::memory_order_acquire) ||
      saving.load(std::memory_order_acquire))
    return;
  if (IOSAudioDriver *driver = Driver();
      driver != nullptr && driver->InputAvailable()) {
    NullPeratorIOSRequestRecordPermission();
    driver->SetInputMonitoring(true);
  }
}

void StopMonitoring() {
  if (IOSAudioDriver *driver = Driver(); driver != nullptr)
    driver->SetInputMonitoring(false);
}

std::uint8_t GetSavingProgressPercent() {
  return savingProgress.load(std::memory_order_acquire);
}

bool DidLastRecordingCaptureAudio() {
  JoinCompletedSavingThread();
  return lastSavedAudio.load(std::memory_order_acquire);
}

std::uint32_t GetRecordingElapsedMilliseconds() {
  IOSAudioDriver *driver = Driver();
  if (recording.load(std::memory_order_acquire) && driver != nullptr) {
    return static_cast<std::uint32_t>(
        (driver->CapturedInputFrames() * 1000ULL) / kSampleRate);
  }
  return elapsedMs.load(std::memory_order_acquire);
}

std::uint16_t GetRecordingInputPeak() {
  IOSAudioDriver *driver = Driver();
  return driver == nullptr ? 0U : driver->InputPeak();
}
