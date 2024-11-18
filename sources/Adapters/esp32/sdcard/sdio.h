// SD card access using SDIO for RP2040 platform.
// This module contains the low-level SDIO bus implementation using
// the PIO peripheral. The high-level commands are in sd_card_sdio.cpp.

#ifndef SDIO_H_
#define SDIO_H_
#include "Adapters/esp32/platform/platform.h"
#include <stdlib.h>
#include <stdint.h>
#include <cstring>

typedef void (*sd_callback_t)(uint32_t bytes_complete);
uint32_t millis(void);

enum sdio_status_t {
  SDIO_OK = 0,
  SDIO_BUSY = 1,
  SDIO_ERR_RESPONSE_TIMEOUT = 2, // Timed out waiting for response from card
  SDIO_ERR_RESPONSE_CRC = 3,     // Response CRC is wrong
  SDIO_ERR_RESPONSE_CODE =
      4, // Response command code does not match what was sent
  SDIO_ERR_DATA_TIMEOUT = 5, // Timed out waiting for data block
  SDIO_ERR_DATA_CRC = 6,     // CRC for data packet is wrong
  SDIO_ERR_WRITE_CRC = 7,    // Card reports bad CRC for write
  SDIO_ERR_WRITE_FAIL = 8,   // Card reports write failure
};

#define SDIO_BLOCK_SIZE 512
#define SDIO_WORDS_PER_BLOCK 128

#endif
