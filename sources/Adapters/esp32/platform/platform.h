#ifndef _PLATFORM_ESP32_H_
#define _PLATFORM_ESP32_H_

#include "gpio.h"
#include <stdint.h>

void board_init();

void platform_init();

uint16_t get_io_expander_input();

#endif
