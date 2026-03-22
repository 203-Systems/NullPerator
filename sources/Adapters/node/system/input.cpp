#include "input.h"
#include "Adapters/node/platform/platform.h"
#include "driver/gpio.h"
#include "esp_log.h"

uint16_t key_cache = 0xFFFF;
uint32_t menu_active_since = 0;
uint32_t start_pressed_since = 0;
bool start_short_tap_candidate = false;
bool start_nav_active = false;
bool start_play_pulse_pending = false;
bool start_started_with_alt = false;
bool prev_start_pressed = false;
#define SLEEP_HOLD_TIME 2000
#define START_TAP_TIME_MS 200

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

  const bool alt_pressed = (key_input & (1u << INPUT_SELECT)) != 0;
  remapped |= (alt_pressed ? KEY_ALT : 0u);

  // Treat a quick standalone START tap as a one-shot KEY_PLAY pulse.
  // Longer holds, or START combined with any other key, keep the original
  // KEY_NAV behavior. If ALT was already held when START went down, route the
  // held START behavior to KEY_PLAY instead of KEY_NAV.
  const bool start_pressed = (key_input & (1u << INPUT_START)) != 0;
  const uint16_t other_keys = remapped;
  const uint32_t now = esp_log_timestamp();

  if (start_pressed) {
    if (!prev_start_pressed) {
      start_started_with_alt = alt_pressed;
    }

    if (!start_short_tap_candidate && !start_nav_active) {
      if (other_keys == 0u) {
        start_short_tap_candidate = true;
        start_pressed_since = now;
      } else {
        start_nav_active = true;
      }
    }

    if (start_short_tap_candidate) {
      const bool chord_started = other_keys != 0u;
      const bool hold_expired = (now - start_pressed_since) >= START_TAP_TIME_MS;
      if (chord_started || hold_expired) {
        start_short_tap_candidate = false;
        start_nav_active = true;
      }
    }
  } else {
    if (start_short_tap_candidate) {
      if ((other_keys == 0u) && ((now - start_pressed_since) < START_TAP_TIME_MS)) {
        start_play_pulse_pending = true;
      }
      start_short_tap_candidate = false;
    }
    start_nav_active = false;
    start_started_with_alt = false;
  }

  if (start_nav_active) {
    remapped |= (start_started_with_alt ? KEY_PLAY : KEY_NAV);
  }

  // // Preserve existing behavior: MENU also maps to KEY_PLAY.
  // remapped |= static_cast<uint16_t>(menu) << 8;

  if (start_play_pulse_pending) {
    remapped |= KEY_PLAY;
    start_play_pulse_pending = false;
  }

  if (__builtin_popcount(remapped ^ key_cache) > 3 && (key_cache != 0xFFFF)) {
    return key_cache;
  }

  if (remapped != key_cache) {
    ESP_LOGI("INPUT", "IOX raw: 0x%04X -> key mask: 0x%04X", key_input,
             remapped);
    key_cache = remapped;
  }

  prev_start_pressed = start_pressed;

  return remapped;
}
