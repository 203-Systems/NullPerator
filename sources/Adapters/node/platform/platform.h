#ifndef _PLATFORM_ESP32_H_
#define _PLATFORM_ESP32_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include "System/Process/SysMutex.h"
#endif

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void board_init();

void platform_init();

esp_err_t audio_codec_write(void* buffer, size_t len, size_t* bytes_written,
                            uint32_t timeout_ms);
esp_err_t audio_codec_set_volume(int volume);
int audio_codec_get_volume(void);
esp_err_t audio_codec_set_mute(bool enable);

typedef enum {
    headphone_out,
    line_in
} audio_mode;

void switch_audio_mode(audio_mode mode);

void switch_speaker_mode(bool on);

void platform_brightness(uint8_t value);

void enter_sleep();

uint32_t millis(void);
uint32_t micros(void);

#ifdef __cplusplus
}
SysMutex* platform_mutex();
#endif

#endif
