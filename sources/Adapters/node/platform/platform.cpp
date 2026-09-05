#include "platform.h"
#include "Adapters/node/hal/nullperator/nullperatorHAL.h"
#include "Adapters/node/mutex/Mutex.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <atomic>

namespace {
audio_mode g_audioMode = headphone_out;
bool g_speakerEnabled = false;
std::atomic<bool> g_audioMuted{false};
int g_audioResumeVolume = 50;

esp_err_t apply_audio_route() {
  using namespace NullperatorHAL;

  if (g_audioMode == line_in) {
    esp_err_t ret = Audio::SetOutputMode(Audio::OUTPUT_OFF);
    if (ret != ESP_OK) {
      return ret;
    }
    return Audio::SetInputMode(Audio::INPUT_LINE_IN);
  }

  esp_err_t ret = Audio::SetInputMode(Audio::INPUT_OFF);
  if (ret != ESP_OK) {
    return ret;
  }
  return Audio::SetOutputMode(g_speakerEnabled ? Audio::OUTPUT_SPEAKER
                                               : Audio::OUTPUT_HEADPHONE);
}
} // namespace

extern "C" {
void board_init() { ESP_ERROR_CHECK(NullperatorHAL::Init()); }

void platform_init() {}

void switch_audio_mode(audio_mode mode) {
  g_audioMode = mode;
  ESP_ERROR_CHECK(apply_audio_route());
}

void switch_speaker_mode(bool on) {
  g_speakerEnabled = on;
  ESP_LOGI("AUDIO_ROUTE", "Speaker %s", on ? "ON" : "OFF");
  ESP_ERROR_CHECK(apply_audio_route());
}

void enter_sleep() { NullperatorHAL::System::EnterDeepSleep(); }

uint32_t millis(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t micros(void) { return static_cast<uint32_t>(esp_timer_get_time()); }

void platform_brightness(uint8_t value) {
  ESP_ERROR_CHECK(NullperatorHAL::Display::SetBrightness(value));
}

esp_err_t audio_codec_write(void *buffer, size_t len, size_t *bytes_written,
                            uint32_t timeout_ms) {
  i2s_chan_handle_t txChan = NullperatorHAL::Audio::GetTxChannel();
  if (!txChan) {
    return ESP_ERR_INVALID_STATE;
  }
  // Codec volume/mute controls audibility. Keep submitting frames while
  // muted: DMA backpressure is also the sequencer's playback clock. Returning
  // immediately here runs the producer (and MIDI transport) at CPU speed.
  return i2s_channel_write(txChan, buffer, len, bytes_written, timeout_ms);
}

esp_err_t audio_codec_set_volume(int volume) {
  if (volume < 0) {
    volume = 0;
  }
  if (volume > 0) {
    g_audioResumeVolume = volume;
  }
  g_audioMuted.store(volume == 0, std::memory_order_release);
  return NullperatorHAL::Audio::SetVolume(static_cast<uint8_t>(volume));
}

int audio_codec_get_volume(void) {
  return static_cast<int>(NullperatorHAL::Audio::GetVolume());
}

esp_err_t audio_codec_set_mute(bool enable) {
  g_audioMuted.store(enable, std::memory_order_release);
  if (enable) {
    const int currentVolume = audio_codec_get_volume();
    if (currentVolume > 0) {
      g_audioResumeVolume = currentVolume;
    }
    return NullperatorHAL::Audio::SetVolume(0);
  }
  return NullperatorHAL::Audio::SetVolume(
      static_cast<uint8_t>(g_audioResumeVolume));
}
} // extern "C"

SysMutex *platform_mutex() { return new NodeMutex(); }
