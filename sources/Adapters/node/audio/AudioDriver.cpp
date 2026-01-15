#include "AudioDriver.h"
#include "Adapters/node/platform/platform.h"
#include "Adapters/node/utils/utils.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// mini blank buffer for underrun, initialized to 0
const char NodeAudioDriver::miniBlank_[MINI_BLANK_SIZE * 2 *
                                              sizeof(short)] = {0};

NodeAudioDriver *NodeAudioDriver::instance_ = NULL;
TaskHandle_t audioThreadHandle_ = NULL;
SemaphoreHandle_t core1_audio = NULL;

static volatile unsigned long esp32_sound_pausei, esp32_exit;

void esp32_sound_pause(int yes) { esp32_sound_pausei = yes; }

void NodeAudioDriver::AudioThread(void *arg) {
  while (1) {
    xSemaphoreTake(core1_audio, portMAX_DELAY);
    NodeAudioDriver::BufferNeeded();
  }
}

void NodeAudioDriver::I2SThread(void *arg) {
  while (1) {
     if (instance_->isPlaying_) {
        // Mark current buffer as empty
        pool_[instance_->poolPlayPosition_].empty_ = true;

        int next = (instance_->poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;

        if (pool_[next].empty_) {
          // If buffer underrun, write silence
          size_t written = 0;
          audio_codec_write((void *)miniBlank_, MINI_BLANK_SIZE, &written, portMAX_DELAY);
          // ESP_LOGI("NodeAudioDriver", "Sending Blank as Buffer %d", instance_->poolPlayPosition_);
        } else {
          // Move to next buffer
          instance_->poolPlayPosition_ = next;

          // Write audio data
          size_t written = 0;
          audio_codec_write(pool_[instance_->poolPlayPosition_].buffer_,
                           pool_[instance_->poolPlayPosition_].size_,
                           &written, portMAX_DELAY);
          // ESP_LOGI("NodeAudioDriver", "Sending Buffer %d", instance_->poolPlayPosition_);
      }
      xSemaphoreGive(core1_audio);
    }
  }
}


void NodeAudioDriver::BufferNeeded() {
  // Audio tick processes MIDI among other things
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

  // Initialize audio codec through platform API
  ESP_ERROR_CHECK(audio_codec_init());

  // Set initial volume
  audio_codec_set_volume(volume);

  core1_audio = xSemaphoreCreateCounting(SOUND_BUFFER_COUNT - 1, SOUND_BUFFER_COUNT - 1);

  if (core1_audio == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create semaphore");
    return false;
  }

  xTaskCreatePinnedToCore(NodeAudioDriver::AudioThread, "AudioThread",
                          4096, NULL, 5, &audioThreadHandle_, 1);

  xTaskCreatePinnedToCore(NodeAudioDriver::I2SThread, "I2SThread",
                          4096, NULL, 1, &audioThreadHandle_, 0);

  if (audioThreadHandle_ == NULL) {
    ESP_LOGE("NodeAudioDriver", "Failed to create AudioThread");
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
  if (audioThreadHandle_ != NULL) {
    // Signal the task to stop if necessary
    isPlaying_ = false;

    // Wait for the task to acknowledge (optional, see below)

    // Delete the task
    vTaskDelete(audioThreadHandle_);
    audioThreadHandle_ = NULL;
  }

}

bool NodeAudioDriver::StartDriver() {
  isPlaying_ = true;
  esp32_sound_pause(0);
  startTime_ = millis();
  return true;
};

void NodeAudioDriver::StopDriver() {
  esp32_sound_pause(1);
  isPlaying_ = false;
}

int NodeAudioDriver::GetPlayedBufferPercentage() {
  // TODO: Do this right
  return 0;
};

double NodeAudioDriver::GetStreamTime() {
  return (millis() - startTime_) / 1000.0;
};
