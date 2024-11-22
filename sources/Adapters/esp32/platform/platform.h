#ifndef _PLATFORM_ESP32_H_
#define _PLATFORM_ESP32_H_

#include "gpio.h"
#include <stdint.h>

#include "driver/i2c_master.h"

extern i2c_master_bus_handle_t i2c_handle;

void board_init();

void platform_init();

uint16_t get_io_expander_input();

typedef enum {
    headphone_out,
    line_in
} audio_mode;

void switch_audio_mode(audio_mode mode);

void switch_speaker_mode(bool on);

#endif
