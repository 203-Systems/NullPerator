#include "picoTrackerAudioDriver.h"
#include "Adapters/esp32/platform/platform.h"
#include "Adapters/esp32/utils/utils.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"

#include "ES8388.h"

// mini blank buffer for underrun, initialized to 0
const char picoTrackerAudioDriver::miniBlank_[MINI_BLANK_SIZE * 2 *
                                              sizeof(short)] = {0};

picoTrackerAudioDriver *picoTrackerAudioDriver::instance_ = NULL;
TaskHandle_t audioThreadHandle_ = NULL;
SemaphoreHandle_t core1_audio = NULL;

i2s_chan_handle_t i2s_tx_handle = NULL;
i2s_chan_handle_t i2s_rx_handle = NULL;

ES8388 codec = ES8388();

static volatile unsigned long picoTracker_sound_pausei, picoTracker_exit;

void picoTracker_sound_pause(int yes) { picoTracker_sound_pausei = yes; }

// This calls comes after the call to the same function name in the pico audio
// driver

bool IRAM_ATTR picoTrackerAudioDriver::i2s_tx_overflow_callback(
  i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
  ESP_DRAM_LOGE("picoTrackerAudioDriver", "TX Overflow");
  return false;
}

bool IRAM_ATTR picoTrackerAudioDriver::i2s_tx_done_callback(
    i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
    // Access the DMA buffer for the next transmission
    ESP_DRAM_LOGE("picoTrackerAudioDriver", "TX Done");
    // uint8_t *dma_buffer = (uint8_t *)event->dma_buf;

    // // Check if audio is playing
    // if (instance_->isPlaying_) {
    //     // Mark the current buffer as empty
    //     pool_[instance_->poolPlayPosition_].empty_ = true;

    //     // Determine the next buffer index
    //     uint8_t next = (instance_->poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;

    //     if (pool_[next].empty_) {
    //         // Buffer underrun: fill the DMA buffer with silence (zeros)
    //         memset(dma_buffer, 0, event->size);  // Fill entire DMA buffer with zeros
    //     } else {
    //         // Move to the next buffer
    //         instance_->poolPlayPosition_ = next;
            
    //         ESP_DRAM_LOGE("picoTrackerAudioDriver", "Copy Audio Buffer - Asked for %d, has %d", event->size, pool_[instance_->poolPlayPosition_].size_);
    //         // Copy the next audio buffer into the DMA buffer
    //         memcpy(dma_buffer, pool_[instance_->poolPlayPosition_].buffer_,
    //                event->size);
    //     }

    //     // Signal that the audio buffer is ready for the next chunk
    //     xSemaphoreGiveFromISR(core1_audio, NULL);
    // } else {
    //     // Not playing: fill the DMA buffer with silence (zeros)
    //     memset(dma_buffer, 0, event->size);  // Fill entire DMA buffer with zeros
    // }

    return false;  // Return true to indicate success
}

void picoTrackerAudioDriver::AudioThread(void *arg) {
  while (1) {
    xSemaphoreTake(core1_audio, portMAX_DELAY);
    // picoTrackerAudioDriver::instance_->OnChunkDone();
    // if(picoTrackerAudioDriver::instance_->isPlaying_) {
    picoTrackerAudioDriver::BufferNeeded();
    // }
  }
}

void picoTrackerAudioDriver::I2SThread(void *arg) {
  while (1) {
     if (instance_->isPlaying_) {
        // Mark current buffer as empty
        // ESP_LOGI("picoTrackerAudioDriver", "Buffer Status %d%d%d%d%d%d", pool_[0].empty_ ? 0 : 1, pool_[1].empty_ ? 0 : 1, pool_[2].empty_ ? 0 : 1, pool_[3].empty_ ? 0 : 1, pool_[4].empty_ ? 0 : 1, pool_[5].empty_ ? 0 : 1);

        pool_[instance_->poolPlayPosition_].empty_ = true;

        int next = (instance_->poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;

        if (pool_[next].empty_) {
          // If buffer underrun, write silence
          ESP_ERROR_CHECK(i2s_channel_write(i2s_tx_handle, miniBlank_, MINI_BLANK_SIZE,
                                            NULL, portMAX_DELAY));
          ESP_LOGI("picoTrackerAudioDriver", "Sending Blank as Buffer %d", instance_->poolPlayPosition_);
        } else {
          // Move to next buffer
          instance_->poolPlayPosition_ = next;

          // Write audio data
          ESP_ERROR_CHECK(
              i2s_channel_write(i2s_tx_handle, pool_[instance_->poolPlayPosition_].buffer_,
                                pool_[instance_->poolPlayPosition_].size_,
                                NULL, portMAX_DELAY));
          // ESP_LOGI("picoTrackerAudioDriver", "Sending Buffer %d", instance_->poolPlayPosition_);
      }
      xSemaphoreGive(core1_audio);
    }
  }
}


void picoTrackerAudioDriver::BufferNeeded() {
  // Audio tick processes MIDI among other things
  // TODO: understand tick and buffer size relationship. currently not constant
  // probably not right
  // TODO: This could (should?) go into the main thread. If done tho, we geat a
  // deadlock in malloc mutex due to malloc being called from core1 and isr
  // simultaneously
  // ESP_LOGI("picoTrackerAudioDriver", "BufferNeeded");
  instance_->onAudioBufferTick();

  instance_->OnNewBufferNeeded();
}

picoTrackerAudioDriver::picoTrackerAudioDriver(AudioSettings &settings)
    : AudioDriver(settings) {

  isPlaying_ = false;
  picoTracker_exit = 0;
}

picoTrackerAudioDriver::~picoTrackerAudioDriver() { picoTracker_exit = 1; }

bool picoTrackerAudioDriver::InitDriver() { // New
  instance_ = this;

  // Get configuration values
  Config *config = Config::GetInstance();
  auto audioLevel = config->GetValue("LINEOUT");
  volume_ = config->GetValue("VOLUME");

  codec.init(i2c_handle, 100000);
  codec.outputSelect(outsel_t::OUT1);
  codec.setOutputVolume(33);
  ESP_LOGI("Codec", "Output Volume: %d", codec.getOutputVolume());

  i2s_chan_config_t chan_cfg = {
      .id = I2S_NUM_0,
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = 2,
      .dma_frame_num = 256,
      .auto_clear_after_cb = false,
      .auto_clear_before_cb = false,
      .intr_priority = 7 
    };

  chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle));
  i2s_std_config_t std_cfg = {
      .clk_cfg = 
          {
              .sample_rate_hz = 44100,
              .clk_src = I2S_CLK_SRC_DEFAULT,
              .mclk_multiple = I2S_MCLK_MULTIPLE_256,
          },

      .slot_cfg = 
          {
              .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
              .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
              .slot_mode = I2S_SLOT_MODE_STEREO,
              .slot_mask = I2S_STD_SLOT_BOTH,
              .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
              .ws_pol = false,
              .bit_shift = false,
              .left_align = true,
              .big_endian = false,
              .bit_order_lsb = true,
          },
      .gpio_cfg =
          {
              .mclk = (gpio_num_t)CODEC_MCLK,
              .bclk = (gpio_num_t)CODEC_BCLK,
              .ws = (gpio_num_t)CODEC_WS,
              .dout = (gpio_num_t)CODEC_DOUT,
              .din = (gpio_num_t)CODEC_DIN,
          },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg));

  i2s_event_callbacks_t cbs = {
      .on_recv = NULL,
      .on_recv_q_ovf = NULL,
      .on_sent = NULL,//picoTrackerAudioDriver::i2s_tx_done_callback,
      .on_send_q_ovf = picoTrackerAudioDriver::i2s_tx_overflow_callback,
  };

  ESP_ERROR_CHECK(i2s_channel_register_event_callback(i2s_tx_handle, &cbs, NULL));

  size_t loaded = 0;
  do
  {
    i2s_channel_preload_data(i2s_tx_handle, miniBlank_, MINI_BLANK_SIZE, &loaded);
  } while (loaded == MINI_BLANK_SIZE);
  

  ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
  ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));

  core1_audio = xSemaphoreCreateCounting(SOUND_BUFFER_COUNT - 1, SOUND_BUFFER_COUNT - 1);

  if (core1_audio == NULL) {
    ESP_LOGE("picoTrackerAudioDriver", "Failed to create semaphore");
    return false;
  }

  xTaskCreatePinnedToCore(picoTrackerAudioDriver::AudioThread, "AudioThread",
                          4096, NULL, 5, &audioThreadHandle_, 1);

  xTaskCreatePinnedToCore(picoTrackerAudioDriver::I2SThread, "I2SThread",
                          4096, NULL, 1, &audioThreadHandle_, 0);

  if (audioThreadHandle_ == NULL) {
    ESP_LOGE("picoTrackerAudioDriver", "Failed to create AudioThread");
    return false;
  }

  return true;
}

void picoTrackerAudioDriver::SetVolume(int v) {
  volume_ = (v <= 100) ? v : 100;
  Trace::Debug("Setting volume to %d", volume_);
};

int picoTrackerAudioDriver::GetVolume() { return volume_; };

void picoTrackerAudioDriver::CloseDriver() {
  // Stop the task if it's running
  if (audioThreadHandle_ != NULL) {
    // Signal the task to stop if necessary
    isPlaying_ = false;

    // Wait for the task to acknowledge (optional, see below)

    // Delete the task
    vTaskDelete(audioThreadHandle_);
    audioThreadHandle_ = NULL;
  }

  // Uninstall the I2S driver
  i2s_channel_disable(i2s_tx_handle);
  i2s_channel_disable(i2s_rx_handle);

  // Not sure if I can delete the channel after disabling it
  // https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/_images/i2s_state_machine.png
  i2s_del_channel(i2s_tx_handle);
  i2s_del_channel(i2s_rx_handle);
}

bool picoTrackerAudioDriver::StartDriver() {
  isPlaying_ = true;

  // // Start filling up as many buffers as we have
  // while(xSemaphoreTake(core1_audio, 0) == pdTRUE)
  // {
  //   ESP_ERROR_CHECK(i2s_channel_write(i2s_tx_handle, miniBlank_, MINI_BLANK_SIZE, NULL, portMAX_DELAY));
  //   ESP_LOGI("picoTrackerAudioDriver", "Buffer %d filled", uxSemaphoreGetCount(core1_audio));
  // }


  picoTracker_sound_pause(0);
  startTime_ = millis();

  return true;
};

void picoTrackerAudioDriver::StopDriver() {
  picoTracker_sound_pause(1);
  isPlaying_ = false;
  // i2s_zero_dma_buffer(I2S_NUM_0);
}

// void picoTrackerAudioDriver::OnChunkDone() {
//   if (isPlaying_) {
//     // Mark current buffer as empty
//     pool_[poolPlayPosition_].empty_ = true;

//     int next = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;

//     if (pool_[next].empty_) {
//       // If buffer underrun, write silence
//       ESP_ERROR_CHECK(i2s_channel_write(i2s_tx_handle, miniBlank_, MINI_BLANK_SIZE,
//                                         NULL, portMAX_DELAY));
//     } else {
//       // Move to next buffer
//       poolPlayPosition_ = next;

//       // uint64_t sum = 0;

//       // for (int i = 0; i < SOUND_BUFFER_COUNT * 2; i++) {
//       //   sum += pool_[poolPlayPosition_].buffer_[i];
//       // }

//       // ESP_LOGI("picoTrackerAudioDriver", "Buffer %d played, sum: %llu",
//       //          poolPlayPosition_, sum);

//       // Write audio data
//       ESP_ERROR_CHECK(
//           i2s_channel_write(i2s_tx_handle, pool_[poolPlayPosition_].buffer_,
//                             pool_[poolPlayPosition_].size_ / 2 / sizeof(short),
//                             NULL, portMAX_DELAY));
//     }
//   }
//   else
//   {
//     ESP_ERROR_CHECK(i2s_channel_write(i2s_tx_handle, miniBlank_, MINI_BLANK_SIZE, NULL, portMAX_DELAY));
//   }
// }

int picoTrackerAudioDriver::GetPlayedBufferPercentage() {
  //	return
  // 100-(bufferSize_-bufferPos_-fragSize_)*100/(bufferSize_-fragSize_);
  // TODO: Do this right
  return 0;
};

double picoTrackerAudioDriver::GetStreamTime() {
  return (millis() - startTime_) / 1000.0;
};
