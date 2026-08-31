#include "AudioDriver.h"
#include "Adapters/node/hal/nullperator/audio/audio.h"
#include "Adapters/node/platform/platform.h"
#include "Adapters/node/system/TaskStackTelemetry.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include "config/MemorySections.h"
#include <cstdint>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

uint8_t NodeAudioDriver::miniBlank_[MINI_BLANK_SIZE * 2U * sizeof(int16_t)]
    PICOTRACKER_FAST_AUDIO_BUFFER = {0};

NodeAudioDriver *NodeAudioDriver::instance_ = NULL;
TaskHandle_t audioThreadHandle_ = NULL;
TaskHandle_t i2sThreadHandle_ = NULL;

namespace {
constexpr std::size_t kSoundBufferCount = 4U;
constexpr std::size_t kSoundBufferMaxBytes =
    MAX_SAMPLE_COUNT * 2U * sizeof(int16_t);

struct NodeAudioBuffer {
  uint8_t buffer[kSoundBufferMaxBytes];
  std::size_t size = 0U;
};

NodeAudioBuffer audioBufferPool[kSoundBufferCount];

static QueueHandle_t freeAudioBuffers = NULL;
static QueueHandle_t filledAudioBuffers = NULL;
static StaticQueue_t freeAudioBuffersControl;
static StaticQueue_t filledAudioBuffersControl;
static uint8_t freeAudioBuffersStorage[kSoundBufferCount * sizeof(uint8_t)]
    PICOTRACKER_FAST_DATA;
static uint8_t filledAudioBuffersStorage[kSoundBufferCount * sizeof(uint8_t)]
    PICOTRACKER_FAST_DATA;

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

  uint8_t index = 0;
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

void NodeAudioDriver::AudioThread(void *arg) {
  NodeTaskStackTelemetry stackTelemetry("AudioThread");
  uint8_t bufferIndex = 0;
  while (1) {
    stackTelemetry.Poll();
    if (instance_ == NULL ||
        !instance_->driverPlaying_.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (xQueueReceive(freeAudioBuffers, &bufferIndex, portMAX_DELAY) !=
        pdTRUE) {
      continue;
    }

    if (instance_ == NULL ||
        !instance_->driverPlaying_.load(std::memory_order_acquire)) {
      return_free_buffer(bufferIndex);
      continue;
    }

    instance_->renderBufferIndex_ = bufferIndex;
    instance_->renderBufferQueued_ = false;
    NodeAudioDriver::BufferNeeded();

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
    if (instance_ == NULL ||
        !instance_->driverPlaying_.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (xQueueReceive(filledAudioBuffers, &bufferIndex,
                      pdMS_TO_TICKS(kQueueUnderrunTimeoutMs)) != pdTRUE) {
      size_t written = 0;
      esp_err_t err =
          audio_codec_write(miniBlank_, sizeof(miniBlank_), &written,
                            kI2SWriteTimeoutMs);
      if (err != ESP_OK || written != sizeof(miniBlank_)) {
        ESP_LOGW("NodeAudioDriver",
                 "silence write err=0x%x written=%u expected=%u",
                 (unsigned)err, (unsigned)written, (unsigned)sizeof(miniBlank_));
      }
      continue;
    }

    if (instance_ == NULL ||
        !instance_->driverPlaying_.load(std::memory_order_acquire)) {
      audioBufferPool[bufferIndex].size = 0U;
      return_free_buffer(bufferIndex);
      continue;
    }

    size_t written = 0;
    const std::size_t expected = audioBufferPool[bufferIndex].size;
    esp_err_t err =
        audio_codec_write(audioBufferPool[bufferIndex].buffer, expected,
                          &written, kI2SWriteTimeoutMs);
    if (err != ESP_OK || written != static_cast<size_t>(expected)) {
      ESP_LOGW("NodeAudioDriver",
               "audio write err=0x%x written=%u expected=%u buffer=%u",
               (unsigned)err, (unsigned)written, (unsigned)expected,
               bufferIndex);
    }

    audioBufferPool[bufferIndex].size = 0U;
    return_free_buffer(bufferIndex);
  }
}

void NodeAudioDriver::BufferNeeded() {
  instance_->onAudioBufferTick();
  instance_->OnNewBufferNeeded();
}

void NodeAudioDriver::AddBuffer(short *buffer, int samplecount) {
  int len = samplecount * 2 * sizeof(short);

  if (!driverPlaying_.load(std::memory_order_acquire)) {
    return;
  }

  if (renderBufferIndex_ < 0 ||
      static_cast<std::size_t>(renderBufferIndex_) >= kSoundBufferCount) {
    ESP_LOGW("NodeAudioDriver", "AddBuffer without reserved buffer");
    return;
  }

  if (static_cast<std::size_t>(len) > kSoundBufferMaxBytes) {
    ESP_LOGW("NodeAudioDriver", "Audio buffer exceeded, clamping samples=%d",
             samplecount);
    samplecount = MAX_SAMPLE_COUNT;
    len = static_cast<int>(kSoundBufferMaxBytes);
  }

  uint8_t bufferIndex = static_cast<uint8_t>(renderBufferIndex_);
  memcpy(audioBufferPool[bufferIndex].buffer, buffer,
         static_cast<std::size_t>(len));
  audioBufferPool[bufferIndex].size = static_cast<std::size_t>(len);

  if (xQueueSend(filledAudioBuffers, &bufferIndex, 0) != pdTRUE) {
    ESP_LOGW("NodeAudioDriver", "filled buffer queue full index=%u",
             bufferIndex);
    audioBufferPool[bufferIndex].size = 0U;
    return_free_buffer(bufferIndex);
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

  freeAudioBuffers = xQueueCreateStatic(
      kSoundBufferCount, sizeof(uint8_t), freeAudioBuffersStorage,
      &freeAudioBuffersControl);
  filledAudioBuffers = xQueueCreateStatic(
      kSoundBufferCount, sizeof(uint8_t), filledAudioBuffersStorage,
      &filledAudioBuffersControl);

  if (freeAudioBuffers == NULL || filledAudioBuffers == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create audio queues");
    return false;
  }

  BaseType_t audioTaskCreated = xTaskCreatePinnedToCore(
      NodeAudioDriver::AudioThread, "AudioThread", 8192, NULL,
      kAudioRenderPriority, &audioThreadHandle_, 1);

  BaseType_t i2sTaskCreated = xTaskCreatePinnedToCore(
      NodeAudioDriver::I2SThread, "I2SThread", 3072, NULL,
      kI2SWriterPriority, &i2sThreadHandle_, 0);

  if (audioTaskCreated != pdPASS || i2sTaskCreated != pdPASS) {
    ESP_LOGE("NodeAudioDriver", "Failed to create audio tasks");
    CloseDriver();
    return false;
  }

  return true;
}

void NodeAudioDriver::SetVolume(int v) {
  volume_ = (v <= 100) ? v : 100;
  audio_codec_set_volume(volume_);
  Trace::Debug("Setting volume to %d", volume_);
};

int NodeAudioDriver::GetVolume() { return audio_codec_get_volume(); };

void NodeAudioDriver::CloseDriver() {
  driverPlaying_.store(false, std::memory_order_release);
  wake_audio_queues();
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
  switch_audio_mode(headphone_out);
  driverPlaying_.store(false, std::memory_order_release);
  for (auto &audioBuffer : audioBufferPool) {
    audioBuffer.size = 0U;
  }
  renderBufferIndex_ = -1;
  renderBufferQueued_ = false;
  reset_audio_queues();
  // Publish the initialized buffer and queue state before either worker may
  // begin producing or consuming audio on its pinned core.
  driverPlaying_.store(true, std::memory_order_release);
  startTime_ = millis();
  return true;
};

void NodeAudioDriver::StopDriver() {
  // Close the cross-core gate before queue wakeups can release either worker.
  driverPlaying_.store(false, std::memory_order_release);
  wake_audio_queues();
}

int NodeAudioDriver::GetPlayedBufferPercentage() { return 0; };

double NodeAudioDriver::GetStreamTime() {
  return (millis() - startTime_) / 1000.0;
};
