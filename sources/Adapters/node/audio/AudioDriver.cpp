#include "AudioDriver.h"
#include "Adapters/node/platform/platform.h"
#include "Adapters/node/system/TaskStackTelemetry.h"
#include "Application/Model/Config.h"
#include "AudioTelemetry.h"
#include "System/Console/Trace.h"
#include "config/MemorySections.h"

#include <cstddef>
#include <cstdint>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

uint8_t NodeAudioDriver::miniBlank_
    [MINI_BLANK_SIZE * 2U * sizeof(int16_t)] PICOTRACKER_FAST_AUDIO_BUFFER = {
        0};

NodeAudioDriver *NodeAudioDriver::instance_ = NULL;
TaskHandle_t audioThreadHandle_ = NULL;
TaskHandle_t i2sThreadHandle_ = NULL;

namespace {
constexpr std::size_t kSoundBufferCount = 4U;
constexpr std::uint8_t kWakeToken = 0xFF;

constexpr std::size_t bounded_sample_count(int samplecount) noexcept {
  if (samplecount <= 0) {
    return 0U;
  }
  const std::size_t count = static_cast<std::size_t>(samplecount);
  return count > MAX_SAMPLE_COUNT ? MAX_SAMPLE_COUNT : count;
}

static_assert(bounded_sample_count(-1) == 0U);
static_assert(bounded_sample_count(0) == 0U);
static_assert(bounded_sample_count(MAX_SAMPLE_COUNT) == MAX_SAMPLE_COUNT);
static_assert(bounded_sample_count(MAX_SAMPLE_COUNT + 1) == MAX_SAMPLE_COUNT);

struct NodeAudioBuffer {
  short buffer[MAX_SAMPLE_COUNT * 2U];
  std::size_t size = 0U;
};

NodeAudioBuffer audioBufferPool[kSoundBufferCount];

static QueueHandle_t freeAudioBuffers = NULL;
static QueueHandle_t filledAudioBuffers = NULL;
static StaticQueue_t freeAudioBuffersControl;
static StaticQueue_t filledAudioBuffersControl;
static uint8_t freeAudioBuffersStorage[kSoundBufferCount *
                                       sizeof(uint8_t)] PICOTRACKER_FAST_DATA;
static uint8_t filledAudioBuffersStorage[kSoundBufferCount *
                                         sizeof(uint8_t)] PICOTRACKER_FAST_DATA;
NodeAudioTelemetry audioTelemetry;

constexpr uint32_t kI2SWriteTimeoutMs = 20;
constexpr uint32_t kQueueUnderrunTimeoutMs = 2;
constexpr UBaseType_t kAudioRenderPriority = 10;
constexpr UBaseType_t kI2SWriterPriority = 12;

void reset_audio_queues() {
  if (freeAudioBuffers == NULL || filledAudioBuffers == NULL) {
    return;
  }

  xQueueReset(freeAudioBuffers);
  xQueueReset(filledAudioBuffers);
  for (uint8_t i = 0; i < kSoundBufferCount; ++i) {
    xQueueSend(freeAudioBuffers, &i, 0);
  }
}

void wake_audio_queues() {
  if (freeAudioBuffers == NULL || filledAudioBuffers == NULL) {
    return;
  }

  uint8_t index = kWakeToken;
  xQueueSend(freeAudioBuffers, &index, 0);
  xQueueSend(filledAudioBuffers, &index, 0);
}

void return_free_buffer(uint8_t index) {
  if (freeAudioBuffers == NULL) {
    return;
  }
  if (xQueueSend(freeAudioBuffers, &index, 0) != pdTRUE) {
    ESP_LOGW("NodeAudioDriver", "free buffer queue full index=%u", index);
  }
}
} // namespace

NodeAudioTelemetry &GetNodeAudioTelemetry() { return audioTelemetry; }

void NodeAudioDriver::AudioThread(void *arg) {
  NodeTaskStackTelemetry stackTelemetry("AudioThread");
  uint8_t bufferIndex = 0;
  while (1) {
    stackTelemetry.Poll();
    if (instance_ == NULL || !instance_->workerGate_.TryEnter(0)) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    WorkerGate<2>::Lease lease(instance_->workerGate_, 0);

    if (xQueueReceive(freeAudioBuffers, &bufferIndex, portMAX_DELAY) !=
        pdTRUE) {
      continue;
    }
    if (bufferIndex == kWakeToken)
      continue;

    if (instance_ == NULL || !instance_->workerGate_.IsRunning()) {
      return_free_buffer(bufferIndex);
      continue;
    }

    instance_->renderBufferIndex_ = bufferIndex;
    instance_->renderBufferQueued_ = false;
    instance_->renderedFrames_ = 0;
    const std::uint32_t renderStart = micros();
    NodeAudioDriver::BufferNeeded();
    audioTelemetry.RecordRender(micros() - renderStart,
                                instance_->renderedFrames_);

    if (!instance_->renderBufferQueued_) {
      audioBufferPool[bufferIndex].size = 0U;
      return_free_buffer(bufferIndex);
    }
    instance_->renderBufferIndex_ = -1;
  }
}

void NodeAudioDriver::I2SThread(void *arg) {
  NodeTaskStackTelemetry stackTelemetry("I2SThread");
  uint8_t bufferIndex = 0;
  while (1) {
    stackTelemetry.Poll();
    if (instance_ == NULL || !instance_->workerGate_.TryEnter(1)) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    WorkerGate<2>::Lease lease(instance_->workerGate_, 1);

    if (xQueueReceive(filledAudioBuffers, &bufferIndex,
                      pdMS_TO_TICKS(kQueueUnderrunTimeoutMs)) != pdTRUE) {
      if (!instance_->workerGate_.IsRunning())
        continue;
      audioTelemetry.RecordStarvation();
      size_t written = 0;
      esp_err_t err = audio_codec_write(miniBlank_, sizeof(miniBlank_),
                                        &written, kI2SWriteTimeoutMs);
      if (err != ESP_OK || written != sizeof(miniBlank_)) {
        audioTelemetry.RecordWriteError();
      }
      continue;
    }
    if (bufferIndex == kWakeToken)
      continue;

    if (instance_ == NULL || !instance_->workerGate_.IsRunning()) {
      audioBufferPool[bufferIndex].size = 0U;
      return_free_buffer(bufferIndex);
      continue;
    }

    size_t written = 0;
    const std::size_t expected = audioBufferPool[bufferIndex].size;
    esp_err_t err = audio_codec_write(audioBufferPool[bufferIndex].buffer,
                                      expected, &written, kI2SWriteTimeoutMs);
    if (err != ESP_OK || written != static_cast<size_t>(expected)) {
      audioTelemetry.RecordWriteError();
    }

    audioBufferPool[bufferIndex].size = 0U;
    return_free_buffer(bufferIndex);
  }
}

void NodeAudioDriver::BufferNeeded() {
  instance_->onAudioBufferTick();
  instance_->OnNewBufferNeeded();
}

std::span<short> NodeAudioDriver::GetOutputBuffer() {
  if (!workerGate_.IsRunning() || renderBufferQueued_ ||
      renderBufferIndex_ < 0 ||
      static_cast<std::size_t>(renderBufferIndex_) >= kSoundBufferCount)
    return {};
  return audioBufferPool[renderBufferIndex_].buffer;
}

void NodeAudioDriver::AddBuffer(short *buffer, int samplecount) {
  if (buffer == nullptr || samplecount <= 0 || renderBufferQueued_ ||
      !workerGate_.IsRunning()) {
    return;
  }

  if (renderBufferIndex_ < 0 ||
      static_cast<std::size_t>(renderBufferIndex_) >= kSoundBufferCount) {
    ESP_LOGW("NodeAudioDriver", "AddBuffer without reserved buffer");
    return;
  }

  const std::size_t sampleCount = bounded_sample_count(samplecount);
  if (sampleCount != static_cast<std::size_t>(samplecount)) {
    ESP_LOGW("NodeAudioDriver", "Audio buffer exceeded, clamping samples=%d",
             samplecount);
  }
  const std::size_t len = sampleCount * 2U * sizeof(int16_t);
  renderedFrames_ = static_cast<std::uint32_t>(sampleCount);

  uint8_t bufferIndex = static_cast<uint8_t>(renderBufferIndex_);
  // AudioOutDriver normally converts directly into this reserved queue slot.
  // Keep the copy contract for callers submitting their own PCM storage.
  if (buffer != audioBufferPool[bufferIndex].buffer)
    memcpy(audioBufferPool[bufferIndex].buffer, buffer, len);
  audioBufferPool[bufferIndex].size = len;

  if (xQueueSend(filledAudioBuffers, &bufferIndex, 0) != pdTRUE) {
    ESP_LOGW("NodeAudioDriver", "filled buffer queue full index=%u",
             bufferIndex);
    audioBufferPool[bufferIndex].size = 0U;
    // AudioThread still owns this reservation and returns it exactly once.
    return;
  }

  renderBufferQueued_ = true;
}

NodeAudioDriver::NodeAudioDriver(AudioSettings &settings)
    : AudioDriver(settings) {}

NodeAudioDriver::~NodeAudioDriver() = default;

bool NodeAudioDriver::InitDriver() {
  instance_ = this;

  Config *config = Config::GetInstance();
  uint8_t volume = 40;
  if (config) {
    if (Variable *outputVolume =
            config->FindVariable(FourCC::VarOutputVolume)) {
      volume = outputVolume->GetInt();
    }
  }

  ESP_LOGI("NodeAudioDriver", "Loaded Audio Volume %d", volume);

  audio_codec_set_volume(volume);
  audio_codec_set_mute(false);
  switch_audio_mode(headphone_out);

  freeAudioBuffers =
      xQueueCreateStatic(kSoundBufferCount, sizeof(uint8_t),
                         freeAudioBuffersStorage, &freeAudioBuffersControl);
  filledAudioBuffers =
      xQueueCreateStatic(kSoundBufferCount, sizeof(uint8_t),
                         filledAudioBuffersStorage, &filledAudioBuffersControl);

  if (freeAudioBuffers == NULL || filledAudioBuffers == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create audio queues");
    return false;
  }

  BaseType_t audioTaskCreated = xTaskCreatePinnedToCore(
      NodeAudioDriver::AudioThread, "AudioThread", 8192, NULL,
      kAudioRenderPriority, &audioThreadHandle_, 1);

  BaseType_t i2sTaskCreated =
      xTaskCreatePinnedToCore(NodeAudioDriver::I2SThread, "I2SThread", 3072,
                              NULL, kI2SWriterPriority, &i2sThreadHandle_, 0);

  if (audioTaskCreated != pdPASS || i2sTaskCreated != pdPASS) {
    ESP_LOGE("NodeAudioDriver", "Failed to create audio tasks");
    CloseDriver();
    return false;
  }

  return true;
}

void NodeAudioDriver::SetVolume(int v) {
  const int volume = (v <= 100) ? v : 100;
  audio_codec_set_volume(volume);
  Trace::Debug("Setting volume to %d", volume);
};

int NodeAudioDriver::GetVolume() { return audio_codec_get_volume(); };

void NodeAudioDriver::CloseDriver() {
  StopDriver();
  switch_speaker_mode(false);
  if (audioThreadHandle_ != NULL) {
    vTaskDelete(audioThreadHandle_);
    audioThreadHandle_ = NULL;
  }
  if (i2sThreadHandle_ != NULL) {
    vTaskDelete(i2sThreadHandle_);
    i2sThreadHandle_ = NULL;
  }
  if (freeAudioBuffers != NULL) {
    vQueueDelete(freeAudioBuffers);
    freeAudioBuffers = NULL;
  }
  if (filledAudioBuffers != NULL) {
    vQueueDelete(filledAudioBuffers);
    filledAudioBuffers = NULL;
  }
}

bool NodeAudioDriver::StartDriver() {
  StopDriver();
  switch_audio_mode(headphone_out);
  for (auto &audioBuffer : audioBufferPool) {
    audioBuffer.size = 0U;
  }
  renderBufferIndex_ = -1;
  renderBufferQueued_ = false;
  reset_audio_queues();
  // Publish the initialized buffer and queue state before either worker may
  // begin producing or consuming audio on its pinned core.
  startTime_ = millis();
  workerGate_.Start();
  return true;
};

void NodeAudioDriver::StopDriver() {
  // Close the cross-core gate before queue wakeups can release either worker.
  // The lifecycle owner must call this outside the mixer lock: an in-flight
  // callback is allowed to finish and release that lock before teardown.
  workerGate_.Stop();
  wake_audio_queues();
  while (!workerGate_.IsIdle())
    vTaskDelay(pdMS_TO_TICKS(1));
}

int NodeAudioDriver::GetPlayedBufferPercentage() { return 0; };

double NodeAudioDriver::GetStreamTime() {
  return (millis() - startTime_) / 1000.0;
};
