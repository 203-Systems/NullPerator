#include "input.h"
#include "Adapters/node/platform/platform.h"
#include "driver/gpio.h"
#include "esp_log.h"

uint16_t key_cache = 0xFFFF;
uint32_t menu_active_since = 0;
#define SLEEP_HOLD_TIME 1000

uint16_t scanKeys() {
#ifdef USB_REMOTE_UI_INPUT
  // This reads a byte from USB serial input in non-blocking way, by using
  // param of 0 for immediate timeout and check for return of 255 for no
  // char read result ref: https://forums.raspberrypi.com/viewtopic.php?t=303964
  // the byte is expected to be sent by the remote UI client in the same bitmask
  // format as is returned by the esp32 hardware reading the gpio pins
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

  uint16_t remapped = 0;

  const bool menu_pressed = (gpio_get_level((gpio_num_t)INPUT_MENU_PIN) == 0);
  const uint16_t menu = menu_pressed ? 1u : 0u;

  // Sleep logic (MENU button hold).
  if (menu && (menu_active_since == 0)) {
    menu_active_since = esp_log_timestamp();
  } else if (!menu) {
    menu_active_since = 0;
  } else if (menu_active_since &&
             (esp_log_timestamp() - menu_active_since > SLEEP_HOLD_TIME)) {
    enter_sleep();
  }

  // Buttons are wired to GND, so the expander is configured with polarity
  // inversion in board_init() and reads active-high for pressed buttons here.
  const uint16_t key_input = get_io_expander_input();

  remapped |= ((key_input & (1u << INPUT_LEFT)) ? KEY_LEFT : 0u);
  remapped |= ((key_input & (1u << INPUT_DOWN)) ? KEY_DOWN : 0u);
  remapped |= ((key_input & (1u << INPUT_RIGHT)) ? KEY_RIGHT : 0u);
  remapped |= ((key_input & (1u << INPUT_UP)) ? KEY_UP : 0u);

  remapped |= ((key_input & (1u << INPUT_B)) ? KEY_EDIT : 0u);
  remapped |= ((key_input & (1u << INPUT_A)) ? KEY_ENTER : 0u);

  remapped |= ((key_input & (1u << INPUT_SELECT)) ? KEY_ALT : 0u);
  remapped |= ((key_input & (1u << INPUT_START)) ? KEY_NAV : 0u);

  // Preserve existing behavior: MENU also maps to KEY_PLAY.
  remapped |= static_cast<uint16_t>(menu) << 8;

  if (__builtin_popcount(remapped ^ key_cache) > 3 && (key_cache != 0xFFFF)) {
    return key_cache;
  }

  if (remapped != key_cache) {
    ESP_LOGI("INPUT", "IOX raw: 0x%04X -> key mask: 0x%04X", key_input,
             remapped);
    key_cache = remapped;
  }

  return remapped;
}
