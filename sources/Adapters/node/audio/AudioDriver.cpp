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
#include "freertos/semphr.h"
#include "freertos/task.h"

// mini blank buffer for underrun, initialized to 0
uint8_t NodeAudioDriver::miniBlank_[MINI_BLANK_SIZE * 2U * sizeof(int16_t)] = {0};

NodeAudioDriver *NodeAudioDriver::instance_ = NULL;
TaskHandle_t audioThreadHandle_ = NULL;
TaskHandle_t i2sThreadHandle_ = NULL;
SemaphoreHandle_t core1_audio = NULL;
static StaticSemaphore_t core1_audio_buffer;

static volatile unsigned long esp32_sound_pausei, esp32_exit;

static void reset_audio_fill_semaphore() {
  if (core1_audio == NULL) {
    return;
  }

  while (xSemaphoreTake(core1_audio, 0) == pdTRUE) {
  }
}

void esp32_sound_pause(int yes) { esp32_sound_pausei = yes; }

void NodeAudioDriver::AudioThread(void *arg) {
  while (1) {
    xSemaphoreTake(core1_audio, portMAX_DELAY);
    if (instance_ == NULL || !instance_->isPlaying_) {
      continue;
    }
    NodeAudioDriver::BufferNeeded();
  }
}

void NodeAudioDriver::I2SThread(void *arg) {
  while (1) {
    if (instance_ == NULL || !instance_->isPlaying_) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    // This mirrors the picoTracker flow: after one chunk finishes writing,
    // free the just-played slot, then advance to the next queued buffer.
    pool_[instance_->poolPlayPosition_].empty_ = true;

    int next = (instance_->poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;

    if (pool_[next].empty_) {
      size_t written = 0;
      esp_err_t err =
          audio_codec_write(miniBlank_, sizeof(miniBlank_), &written,
                            portMAX_DELAY);
      if (err != ESP_OK || written != sizeof(miniBlank_)) {
        ESP_LOGW("NodeAudioDriver",
                 "silence write err=0x%x written=%u expected=%u play=%d next=%d",
                 (unsigned)err, (unsigned)written,
                 (unsigned)sizeof(miniBlank_), instance_->poolPlayPosition_,
                 next);
      }
    } else {
      instance_->poolPlayPosition_ = next;

      size_t written = 0;
      int expected = pool_[instance_->poolPlayPosition_].size_;
      esp_err_t err = audio_codec_write(
          pool_[instance_->poolPlayPosition_].buffer_, expected, &written,
          portMAX_DELAY);
      if (err != ESP_OK || written != static_cast<size_t>(expected)) {
        ESP_LOGW("NodeAudioDriver",
                 "audio write err=0x%x written=%u expected=%u play=%d next=%d",
                 (unsigned)err, (unsigned)written, (unsigned)expected,
                 instance_->poolPlayPosition_, next);
      }
    }
    xSemaphoreGive(core1_audio);
  }
}


void NodeAudioDriver::BufferNeeded() {
  instance_->onAudioBufferTick();
  instance_->OnNewBufferNeeded();
}

NodeAudioDriver::NodeAudioDriver(AudioSettings &settings)
    : AudioDriver(settings) {

  isPlaying_ = false;
  esp32_exit = 0;
}

NodeAudioDriver::~NodeAudioDriver() { esp32_exit = 1; }

bool NodeAudioDriver::InitDriver() { // New
  instance_ = this;

  // Get configuration values
  Config *config = Config::GetInstance();
  uint8_t volume = 40;
  if (config) {
    if (Variable *outputVolume =
            config->FindVariable(FourCC::VarOutputVolume)) {
      volume = outputVolume->GetInt();
    }
  }


  ESP_LOGI("NodeAudioDriver", "Loaded Audio Volume %d", volume);

  // Set initial volume
  audio_codec_set_volume(volume);
  audio_codec_set_mute(false);
  switch_audio_mode(headphone_out);
  switch_speaker_mode(false);

  core1_audio = xSemaphoreCreateCountingStatic(SOUND_BUFFER_COUNT - 1, 0,
                                               &core1_audio_buffer);

  if (core1_audio == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create semaphore");
    return false;
  }

  xTaskCreatePinnedToCore(NodeAudioDriver::AudioThread, "AudioThread",
                          8192, NULL, 5, &audioThreadHandle_, 1);

  xTaskCreatePinnedToCore(NodeAudioDriver::I2SThread, "I2SThread",
                          4096, NULL, 6, &i2sThreadHandle_, 0);

  if (audioThreadHandle_ == NULL || i2sThreadHandle_ == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create audio tasks");
    return false;
  }

  return true;
}

void NodeAudioDriver::SetVolume(int v) {
  volume_ = (v <= 100) ? v : 100;
  audio_codec_set_volume(volume_);
  Trace::Debug("Setting volume to %d", volume_);
};

int NodeAudioDriver::GetVolume() {
  return audio_codec_get_volume();
};

void NodeAudioDriver::CloseDriver() {
  // Stop the task if it's running
  isPlaying_ = false;
  switch_speaker_mode(false);
  if (audioThreadHandle_ != NULL) {
    vTaskDelete(audioThreadHandle_);
    audioThreadHandle_ = NULL;
  }
  if (i2sThreadHandle_ != NULL) {
    vTaskDelete(i2sThreadHandle_);
    i2sThreadHandle_ = NULL;
  }
}

bool NodeAudioDriver::StartDriver() {
  switch_audio_mode(headphone_out);
  switch_speaker_mode(false);
  isPlaying_ = true;
  for (int i = 0; i < SOUND_BUFFER_COUNT; ++i) {
    pool_[i].size_ = 0;
    pool_[i].empty_ = true;
  }
  poolQueuePosition_ = 0;
  poolPlayPosition_ = SOUND_BUFFER_COUNT - 1;
  hasData_ = false;
  reset_audio_fill_semaphore();
  for (int i = 0; i < SOUND_BUFFER_COUNT - 1; ++i) {
    xSemaphoreGive(core1_audio);
  }
  esp32_sound_pause(0);
  startTime_ = millis();
  return true;
};

void NodeAudioDriver::StopDriver() {
  esp32_sound_pause(1);
  isPlaying_ = false;
  reset_audio_fill_semaphore();
  switch_speaker_mode(false);
}

int NodeAudioDriver::GetPlayedBufferPercentage() {
  // TODO: Do this right
  return 0;
};

double NodeAudioDriver::GetStreamTime() {
  return (millis() - startTime_) / 1000.0;
};
