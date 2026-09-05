/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * NullPerator I2S recording backend. Capture stays in PSRAM while active and
 * is flushed to FAT only after RX has stopped, keeping SD latency out of the
 * real-time input loop.
 */

#include "record.h"

#include "Adapters/node/hal/nullperator/audio/audio.h"
#include "Adapters/node/platform/platform.h"
#include "Services/Audio/WavHeader.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::uint32_t kSampleRate = 44100U;
constexpr std::uint32_t kMaximumSeconds = 30U;
constexpr std::uint32_t kMinimumFrames = kSampleRate;
constexpr std::size_t kPsramReserveBytes = 256U * 1024U;
constexpr std::size_t kCaptureSamples = 2048U;
constexpr std::size_t kWriteChunkBytes = 4096U;
constexpr std::uint32_t kReadTimeoutMs = 100U;
constexpr std::uint32_t kStopTimeoutMs = 35000U;

enum class CaptureMode : std::uint8_t { Idle, Monitor, Record };

std::array<std::int16_t, kCaptureSamples> captureBuffer{};
std::atomic<CaptureMode> requestedMode{CaptureMode::Idle};
std::atomic<CaptureMode> activeMode{CaptureMode::Idle};
std::atomic<bool> saving{false};
std::atomic<bool> lastSavedAudio{false};
std::atomic<std::uint8_t> savingProgress{0U};
std::atomic<std::uint16_t> inputPeak{0U};
std::atomic<std::uint32_t> elapsedMs{0U};
std::atomic<RecordSource> inputSource{LineIn};

TaskHandle_t captureTask = nullptr;
StaticSemaphore_t stoppedSemaphoreStorage{};
SemaphoreHandle_t stoppedSemaphore = nullptr;

std::uint8_t *recordingBuffer = nullptr;
std::size_t recordingCapacity = 0U;
std::size_t recordingBytes = 0U;
std::uint16_t recordingChannels = 2U;
std::array<char, PFILENAME_SIZE> recordingPath{};

NullperatorHAL::Audio::InputMode_t HalInputMode(RecordSource source) {
  using namespace NullperatorHAL::Audio;
  switch (source) {
  case LineIn:
    return INPUT_LINE_IN;
  case OnboardMic:
    return INPUT_ONBOARD_MIC;
  case HeadphoneMic:
    return INPUT_EARPHONE_MIC;
  }
  return INPUT_OFF;
}

void DrainStoppedSemaphore() {
  if (stoppedSemaphore == nullptr)
    return;
  while (xSemaphoreTake(stoppedSemaphore, 0) == pdTRUE) {
  }
}

void ReleaseRecordingBuffer() {
  if (recordingBuffer != nullptr)
    heap_caps_free(recordingBuffer);
  recordingBuffer = nullptr;
  recordingCapacity = 0U;
  recordingBytes = 0U;
}

void UpdatePeak(const std::int16_t *samples, std::size_t count) {
  std::uint16_t peak = 0U;
  for (std::size_t index = 0U; index < count; ++index) {
    const std::int32_t sample = samples[index];
    const std::uint16_t magnitude = static_cast<std::uint16_t>(
        sample < 0 ? std::min<std::int32_t>(-sample, 32767) : sample);
    peak = std::max(peak, magnitude);
  }
  inputPeak.store(peak, std::memory_order_release);
}

bool PersistRecording() {
  if (recordingBuffer == nullptr || recordingBytes == 0U ||
      recordingPath[0] == '\0')
    return false;
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr)
    return false;
  FileHandle file = fileSystem->Open(recordingPath.data(), "w");
  if (!file)
    return false;
  if (!WavHeaderWriter::WriteHeader(file.get(), kSampleRate, recordingChannels,
                                    16U)) {
    file.reset();
    (void)fileSystem->DeleteFile(recordingPath.data());
    return false;
  }

  std::size_t written = 0U;
  while (written < recordingBytes) {
    const std::size_t chunk =
        std::min(kWriteChunkBytes, recordingBytes - written);
    if (file->Write(recordingBuffer + written, 1, static_cast<int>(chunk)) !=
        static_cast<int>(chunk)) {
      file.reset();
      (void)fileSystem->DeleteFile(recordingPath.data());
      return false;
    }
    written += chunk;
    savingProgress.store(
        static_cast<std::uint8_t>((written * 100U) / recordingBytes),
        std::memory_order_release);
  }

  const std::uint32_t frames = static_cast<std::uint32_t>(
      recordingBytes / (recordingChannels * sizeof(std::int16_t)));
  if (!file->Sync() ||
      !WavHeaderWriter::UpdateFileSize(file.get(), frames, recordingChannels,
                                       sizeof(std::int16_t)) ||
      !file->Sync()) {
    file.reset();
    (void)fileSystem->DeleteFile(recordingPath.data());
    return false;
  }
  return true;
}

void CaptureTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    CaptureMode mode = requestedMode.load(std::memory_order_acquire);
    if (mode == CaptureMode::Idle)
      continue;

    i2s_chan_handle_t rx = NullperatorHAL::Audio::GetRxChannel();
    if (rx == nullptr || i2s_channel_enable(rx) != ESP_OK) {
      requestedMode.store(CaptureMode::Idle, std::memory_order_release);
      activeMode.store(CaptureMode::Idle, std::memory_order_release);
      if (mode == CaptureMode::Record)
        ReleaseRecordingBuffer();
      if (stoppedSemaphore != nullptr)
        xSemaphoreGive(stoppedSemaphore);
      continue;
    }

    activeMode.store(mode, std::memory_order_release);
    inputPeak.store(0U, std::memory_order_release);
    elapsedMs.store(0U, std::memory_order_release);
    while (requestedMode.load(std::memory_order_acquire) == mode) {
      std::size_t bytesRead = 0U;
      const esp_err_t result = i2s_channel_read(
          rx, captureBuffer.data(), captureBuffer.size() * sizeof(std::int16_t),
          &bytesRead, pdMS_TO_TICKS(kReadTimeoutMs));
      if (result == ESP_ERR_TIMEOUT)
        continue;
      if (result != ESP_OK) {
        Trace::Error("RECORD", "I2S RX failed: %d", result);
        break;
      }
      const std::size_t samplesRead = bytesRead / sizeof(std::int16_t);
      UpdatePeak(captureBuffer.data(), samplesRead);
      if (mode != CaptureMode::Record)
        continue;

      const std::size_t available = recordingCapacity - recordingBytes;
      const std::size_t copyBytes = std::min(bytesRead, available);
      if (copyBytes != 0U) {
        std::memcpy(recordingBuffer + recordingBytes, captureBuffer.data(),
                    copyBytes);
        recordingBytes += copyBytes;
        const std::uint64_t frameBytes =
            recordingChannels * sizeof(std::int16_t);
        elapsedMs.store(static_cast<std::uint32_t>((recordingBytes * 1000ULL) /
                                                   (frameBytes * kSampleRate)),
                        std::memory_order_release);
      }
      if (copyBytes != bytesRead) {
        requestedMode.store(CaptureMode::Idle, std::memory_order_release);
        break;
      }
    }

    CaptureMode expected = mode;
    (void)requestedMode.compare_exchange_strong(expected, CaptureMode::Idle,
                                                std::memory_order_acq_rel);
    (void)i2s_channel_disable(rx);
    activeMode.store(CaptureMode::Idle, std::memory_order_release);
    inputPeak.store(0U, std::memory_order_release);

    if (mode == CaptureMode::Record) {
      saving.store(true, std::memory_order_release);
      savingProgress.store(0U, std::memory_order_release);
      const bool saved = PersistRecording();
      lastSavedAudio.store(saved, std::memory_order_release);
      savingProgress.store(saved ? 100U : 0U, std::memory_order_release);
      saving.store(false, std::memory_order_release);
      ReleaseRecordingBuffer();
    }
    if (stoppedSemaphore != nullptr)
      xSemaphoreGive(stoppedSemaphore);
  }
}

bool EnsureCaptureTask() {
  if (captureTask != nullptr)
    return true;
  stoppedSemaphore = xSemaphoreCreateBinaryStatic(&stoppedSemaphoreStorage);
  if (stoppedSemaphore == nullptr)
    return false;
  return xTaskCreatePinnedToCore(CaptureTask, "RecordCapture", 4096, nullptr, 5,
                                 &captureTask, 0) == pdPASS;
}

bool WaitUntilStopped(std::uint32_t timeoutMs) {
  if (activeMode.load(std::memory_order_acquire) == CaptureMode::Idle &&
      requestedMode.load(std::memory_order_acquire) == CaptureMode::Idle &&
      !saving.load(std::memory_order_acquire))
    return true;
  if (stoppedSemaphore == nullptr)
    return false;
  const TickType_t timeout =
      timeoutMs == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
  return xSemaphoreTake(stoppedSemaphore, timeout) == pdTRUE;
}

} // namespace

void Record(void *) { CaptureTask(nullptr); }

bool StartRecording(const char *filename, std::uint8_t /*threshold*/,
                    std::uint32_t milliseconds) {
  if (filename == nullptr || filename[0] == '\0' || !EnsureCaptureTask())
    return false;
  StopMonitoring();
  SetInputSource(inputSource.load(std::memory_order_acquire));

  recordingChannels =
      inputSource.load(std::memory_order_acquire) == LineIn ? 2U : 1U;
  const std::uint32_t durationMs =
      milliseconds == 0U ? kMaximumSeconds * 1000U : milliseconds;
  const std::uint64_t requestedBytes =
      (static_cast<std::uint64_t>(kSampleRate) * recordingChannels *
       sizeof(std::int16_t) * durationMs) /
      1000U;
  const std::size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const std::size_t usable =
      largest > kPsramReserveBytes ? largest - kPsramReserveBytes : 0U;
  const std::size_t minimumBytes = static_cast<std::size_t>(
      kMinimumFrames * recordingChannels * sizeof(std::int16_t));
  recordingCapacity =
      static_cast<std::size_t>(std::min<std::uint64_t>(requestedBytes, usable));
  recordingCapacity -=
      recordingCapacity % (recordingChannels * sizeof(std::int16_t));
  if (recordingCapacity < minimumBytes)
    return false;
  recordingBuffer = static_cast<std::uint8_t *>(
      heap_caps_malloc(recordingCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (recordingBuffer == nullptr) {
    recordingCapacity = 0U;
    return false;
  }

  std::snprintf(recordingPath.data(), recordingPath.size(), "%s", filename);
  recordingBytes = 0U;
  lastSavedAudio.store(false, std::memory_order_release);
  savingProgress.store(0U, std::memory_order_release);
  DrainStoppedSemaphore();
  requestedMode.store(CaptureMode::Record, std::memory_order_release);
  xTaskNotifyGive(captureTask);
  return true;
}

void StopRecording() {
  RequestStopRecording();
  if (!WaitForRecordingStop(kStopTimeoutMs))
    Trace::Error("RECORD", "Timed out stopping recording");
  FinishStopRecording();
}

void RequestStopRecording() {
  if (requestedMode.load(std::memory_order_acquire) == CaptureMode::Record)
    requestedMode.store(CaptureMode::Idle, std::memory_order_release);
}

bool WaitForRecordingStop(std::uint32_t timeoutMs) {
  return WaitUntilStopped(timeoutMs);
}

void FinishStopRecording() {}

void StartMonitoring() {
  if (!EnsureCaptureTask() || IsRecordingActive() || IsSavingRecording())
    return;
  if (activeMode.load(std::memory_order_acquire) == CaptureMode::Monitor)
    return;
  DrainStoppedSemaphore();
  requestedMode.store(CaptureMode::Monitor, std::memory_order_release);
  xTaskNotifyGive(captureTask);
}

void StopMonitoring() {
  if (requestedMode.load(std::memory_order_acquire) == CaptureMode::Monitor ||
      activeMode.load(std::memory_order_acquire) == CaptureMode::Monitor) {
    requestedMode.store(CaptureMode::Idle, std::memory_order_release);
    (void)WaitUntilStopped(1000U);
  }
  (void)NullperatorHAL::Audio::SetInputMode(NullperatorHAL::Audio::INPUT_OFF);
  switch_audio_mode(headphone_out);
}

void SetInputSource(RecordSource source) {
  const bool restart = IsMonitoringActive();
  if (restart)
    StopMonitoring();
  inputSource.store(source, std::memory_order_release);
  const esp_err_t result =
      NullperatorHAL::Audio::SetInputMode(HalInputMode(source));
  if (result != ESP_OK)
    Trace::Error("RECORD", "Failed to select input source: %d", result);
  if (restart)
    StartMonitoring();
}

bool IsRecordingInputSelectable() { return true; }

bool IsRecordingAvailable() {
  return NullperatorHAL::Audio::GetRxChannel() != nullptr &&
         heap_caps_get_total_size(MALLOC_CAP_SPIRAM) != 0U;
}

bool IsRecordingActive() {
  return requestedMode.load(std::memory_order_acquire) == CaptureMode::Record ||
         activeMode.load(std::memory_order_acquire) == CaptureMode::Record;
}

bool IsMonitoringActive() {
  return requestedMode.load(std::memory_order_acquire) ==
             CaptureMode::Monitor ||
         activeMode.load(std::memory_order_acquire) == CaptureMode::Monitor;
}

bool IsSavingRecording() { return saving.load(std::memory_order_acquire); }

std::uint8_t GetSavingProgressPercent() {
  return savingProgress.load(std::memory_order_acquire);
}

bool DidLastRecordingCaptureAudio() {
  return lastSavedAudio.load(std::memory_order_acquire);
}

std::uint32_t GetRecordingElapsedMilliseconds() {
  return elapsedMs.load(std::memory_order_acquire);
}

std::uint16_t GetRecordingInputPeak() {
  return inputPeak.load(std::memory_order_acquire);
}
