#include "AudioDriver.h"
#include "Adapters/node/platform/platform.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include <cstdint>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

uint8_t NodeAudioDriver::miniBlank_[MINI_BLANK_SIZE * 2U * sizeof(int16_t)] = {0};

NodeAudioDriver *NodeAudioDriver::instance_ = NULL;
TaskHandle_t audioThreadHandle_ = NULL;
TaskHandle_t i2sThreadHandle_ = NULL;

static QueueHandle_t freeAudioBuffers = NULL;
static QueueHandle_t filledAudioBuffers = NULL;
static StaticQueue_t freeAudioBuffersControl;
static StaticQueue_t filledAudioBuffersControl;
static uint8_t freeAudioBuffersStorage[SOUND_BUFFER_COUNT * sizeof(uint8_t)];
static uint8_t filledAudioBuffersStorage[SOUND_BUFFER_COUNT * sizeof(uint8_t)];

static volatile unsigned long esp32_sound_pausei, esp32_exit;

namespace {
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
  for (uint8_t i = 0; i < SOUND_BUFFER_COUNT; ++i) {
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

void esp32_sound_pause(int yes) { esp32_sound_pausei = yes; }

void NodeAudioDriver::AudioThread(void *arg) {
  uint8_t bufferIndex = 0;
  while (1) {
    if (instance_ == NULL || !instance_->isPlaying_) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (xQueueReceive(freeAudioBuffers, &bufferIndex, portMAX_DELAY) !=
        pdTRUE) {
      continue;
    }

    if (instance_ == NULL || !instance_->isPlaying_) {
      return_free_buffer(bufferIndex);
      continue;
    }

    instance_->renderBufferIndex_ = bufferIndex;
    instance_->renderBufferQueued_ = false;
    NodeAudioDriver::BufferNeeded();

    if (!instance_->renderBufferQueued_) {
      pool_[bufferIndex].size_ = 0;
      pool_[bufferIndex].empty_ = true;
      return_free_buffer(bufferIndex);
    }
    instance_->renderBufferIndex_ = -1;
  }
}

void NodeAudioDriver::I2SThread(void *arg) {
  uint8_t bufferIndex = 0;
  while (1) {
    if (instance_ == NULL || !instance_->isPlaying_) {
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

    if (instance_ == NULL || !instance_->isPlaying_) {
      pool_[bufferIndex].size_ = 0;
      pool_[bufferIndex].empty_ = true;
      return_free_buffer(bufferIndex);
      continue;
    }

    instance_->poolPlayPosition_ = bufferIndex;

    size_t written = 0;
    int expected = pool_[bufferIndex].size_;
    esp_err_t err = audio_codec_write(pool_[bufferIndex].buffer_, expected,
                                      &written, kI2SWriteTimeoutMs);
    if (err != ESP_OK || written != static_cast<size_t>(expected)) {
      ESP_LOGW("NodeAudioDriver",
               "audio write err=0x%x written=%u expected=%u buffer=%u",
               (unsigned)err, (unsigned)written, (unsigned)expected,
               bufferIndex);
    }

    pool_[bufferIndex].size_ = 0;
    pool_[bufferIndex].empty_ = true;
    return_free_buffer(bufferIndex);
  }
}

void NodeAudioDriver::BufferNeeded() {
  instance_->onAudioBufferTick();
  instance_->OnNewBufferNeeded();
}

void NodeAudioDriver::AddBuffer(short *buffer, int samplecount) {
  int len = samplecount * 2 * sizeof(short);

  if (!isPlaying_) {
    return;
  }

  if (renderBufferIndex_ < 0 || renderBufferIndex_ >= SOUND_BUFFER_COUNT) {
    ESP_LOGW("NodeAudioDriver", "AddBuffer without reserved buffer");
    return;
  }

  if (len > SOUND_BUFFER_MAX) {
    ESP_LOGW("NodeAudioDriver", "Audio buffer exceeded, clamping samples=%d",
             samplecount);
    samplecount = MAX_SAMPLE_COUNT;
    len = SOUND_BUFFER_MAX;
  }

  uint8_t bufferIndex = static_cast<uint8_t>(renderBufferIndex_);
  memcpy(pool_[bufferIndex].buffer_, (char *)buffer, len);
  pool_[bufferIndex].size_ = len;
  pool_[bufferIndex].empty_ = false;

  if (xQueueSend(filledAudioBuffers, &bufferIndex, 0) != pdTRUE) {
    ESP_LOGW("NodeAudioDriver", "filled buffer queue full index=%u",
             bufferIndex);
    pool_[bufferIndex].size_ = 0;
    pool_[bufferIndex].empty_ = true;
    return_free_buffer(bufferIndex);
    return;
  }

  renderBufferQueued_ = true;
  hasData_ = true;
}

NodeAudioDriver::NodeAudioDriver(AudioSettings &settings)
    : AudioDriver(settings) {
  isPlaying_ = false;
  esp32_exit = 0;
}

NodeAudioDriver::~NodeAudioDriver() { esp32_exit = 1; }

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
  switch_speaker_mode(false);

  freeAudioBuffers = xQueueCreateStatic(
      SOUND_BUFFER_COUNT, sizeof(uint8_t), freeAudioBuffersStorage,
      &freeAudioBuffersControl);
  filledAudioBuffers = xQueueCreateStatic(
      SOUND_BUFFER_COUNT, sizeof(uint8_t), filledAudioBuffersStorage,
      &filledAudioBuffersControl);

  if (freeAudioBuffers == NULL || filledAudioBuffers == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create audio queues");
    return false;
  }

  BaseType_t audioTaskCreated = xTaskCreatePinnedToCore(
      NodeAudioDriver::AudioThread, "AudioThread", 8192, NULL,
      kAudioRenderPriority, &audioThreadHandle_, 1);

  BaseType_t i2sTaskCreated = xTaskCreatePinnedToCore(
      NodeAudioDriver::I2SThread, "I2SThread", 4096, NULL,
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
  isPlaying_ = false;
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
  switch_speaker_mode(false);
  isPlaying_ = false;
  for (int i = 0; i < SOUND_BUFFER_COUNT; ++i) {
    pool_[i].size_ = 0;
    pool_[i].empty_ = true;
  }
  poolQueuePosition_ = 0;
  poolPlayPosition_ = SOUND_BUFFER_COUNT - 1;
  renderBufferIndex_ = -1;
  renderBufferQueued_ = false;
  hasData_ = false;
  reset_audio_queues();
  isPlaying_ = true;
  esp32_sound_pause(0);
  startTime_ = millis();
  return true;
};

void NodeAudioDriver::StopDriver() {
  esp32_sound_pause(1);
  isPlaying_ = false;
  wake_audio_queues();
  switch_speaker_mode(false);
}

int NodeAudioDriver::GetPlayedBufferPercentage() { return 0; };

double NodeAudioDriver::GetStreamTime() {
  return (millis() - startTime_) / 1000.0;
};
