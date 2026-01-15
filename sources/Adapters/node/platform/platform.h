#ifndef _PLATFORM_ESP32_H_
#define _PLATFORM_ESP32_H_

#include "gpio.h"
#include <stdint.h>

#ifdef __cplusplus
#include "System/Process/SysMutex.h"
#endif

#include "driver/i2c_master.h"
#include "esp_err.h"

extern i2c_master_bus_handle_t i2c_handle;

#ifdef __cplusplus
extern "C" {
#endif

void board_init();

void platform_init();

// Audio codec API
esp_err_t audio_codec_init(void);
esp_err_t audio_codec_write(void* buffer, size_t len, size_t* bytes_written, uint32_t timeout_ms);
esp_err_t audio_codec_set_volume(int volume);
int audio_codec_get_volume(void);
esp_err_t audio_codec_set_mute(bool enable);

uint16_t get_io_expander_input();

typedef enum {
    headphone_out,
    line_in
} audio_mode;

void switch_audio_mode(audio_mode mode);

void switch_speaker_mode(bool on);

void enter_sleep();

uint32_t millis(void);
uint32_t micros(void);

#ifdef __cplusplus
}
SysMutex *platform_mutex();
#endif

#endif
