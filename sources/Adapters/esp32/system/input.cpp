#include "input.h"
#include <stdio.h>
#include "Adapters/esp32/platform/platform.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte) \
    (byte & 0x80 ? '1' : '0'), \
    (byte & 0x40 ? '1' : '0'), \
    (byte & 0x20 ? '1' : '0'), \
    (byte & 0x10 ? '1' : '0'), \
    (byte & 0x08 ? '1' : '0'), \
    (byte & 0x04 ? '1' : '0'), \
    (byte & 0x02 ? '1' : '0'), \
    (byte & 0x01 ? '1' : '0')

uint16_t scanKeys() {
#ifdef USB_REMOTE_UI_INPUT
  // This reads a byte from USB serial input in non-blocking way, by using
  // param of 0 for immediate timeout and check for return of 255 for no
  // char read result ref: https://forums.raspberrypi.com/viewtopic.php?t=303964
  // the byte is expected to be sent by the remote UI client in the same bitmask
  // format as is returned by the picotracker hardware reading the gpio pins
  // group by the code below.
  // That bit mask is documented in the KEYPAD_BITS enum.
  char c = getchar_timeout_us(0);
  if (c != 0xFF) {
    // how to encode a 9bit bitmask into a 7bit char? (8th bit is not usable
    // because 0xFF means no result) answer: use MIDI style encoding, if bitmask
    // > 0x40 then set bit 7 to 1 to indicate 2 chars needed and then the next
    // char contains the remaining 3 bits
    int16_t mask = c & 0x3F;
    if (c & 0xFE) {
      // get remaining 3 bits from reading the next char:
      c = getchar_timeout_us(0);
      if (c != 0xFF) {
        mask += (c & 0x7) << 6;
      } else {
        // TODO: error missing follow up char!
      }
    }
    return mask;
  }
#endif
  uint16_t key_input = get_io_expander_input();
  uint16_t remapped = 0;

  bool menu = !gpio_get_level((gpio_num_t)INPUT_MENU_PIN) << 8;

  // See eventMappingPico in picoTrackerGUIWindowImp.cpp for the mapping
  remapped |= ((key_input & (1 << INPUT_LEFT)) ? 1 : 0) << 0;

  remapped |= ((key_input & (1 << INPUT_DOWN)) ? 1 : 0) << 1;

  remapped |= ((key_input & (1 << INPUT_RIGHT)) ? 1 : 0) << 2;

  remapped |= ((key_input & (1 << INPUT_UP)) ? 1 : 0) << 3;

  remapped |= ((key_input & (1 << INPUT_SELECT)) ? 1 : 0) << 4;
  // remapped |= ((key_input & (1 << INPUT_LB)) ? 1 : 0) << 4;
  // remapped |= (((key_input & (1 << INPUT_B)) ? 1 : 0) && menu) << 4;
  // remapped |= ((key_input & (1 << INPUT_B)) ? 1 : 0) << 4;

    remapped |= ((key_input & (1 << INPUT_B)) ? 1 : 0) << 5;

  remapped |= ((key_input & (1 << INPUT_A)) ? 1 : 0) << 6;

  remapped |= ((key_input & (1 << INPUT_START)) ? 1 : 0) << 7;
  // remapped |= ((key_input & (1 << INPUT_RB)) ? 1 : 0) << 7;
  // remapped |= (((key_input & (1 << INPUT_A)) ? 1 : 0) && menu) << 7;
  // remapped |= ((key_input & (1 << INPUT_A)) ? 1 : 0) << 7;
  // remapped |= menu << 7;

  remapped |= menu << 8;

  // ESP_LOGI("INPUT", "raw: %d - key_input: 0b%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c", key_input,
  //       BYTE_TO_BINARY(remapped >> 8), BYTE_TO_BINARY(remapped & 0xFF));
  
  return remapped;
}
