#include "input.h"
#include "Adapters/node/hal/nullperator/input/input.h"
#include "Adapters/node/platform/platform.h"

namespace {
constexpr uint32_t SLEEP_HOLD_TIME_MS = 2000;
constexpr uint32_t START_TAP_TIME_MS = 200;
constexpr uint16_t kUninitializedKeyCache = 0xFFFF;

struct StartButtonState {
  uint32_t pressed_since_ms = 0;
  bool tap_candidate = false;
  bool nav_active = false;
  bool play_pulse_pending = false;
  bool started_with_alt = false;
  bool was_pressed = false;
};

struct InputState {
  uint16_t last_key_mask = kUninitializedKeyCache;
  uint32_t menu_pressed_since_ms = 0;
  StartButtonState start = {};
};

InputState g_input_state = {};

#ifdef USB_REMOTE_UI_INPUT
// Remote UI injects button state over serial using the same compact mask shape
// as local input, so decode it here and let the rest of scanKeys() stay shared.
int16_t read_remote_ui_mask() {
  // The remote UI sends the same button bitmask as hardware, encoded into one
  // or two 7-bit-safe bytes.
  char c = getchar_timeout_us(0);
  if (c == 0xFF) {
    return -1;
  }

  int16_t mask = c & 0x3F;
  if (c & 0xFE) {
    c = getchar_timeout_us(0);
    if (c != 0xFF) {
      mask += (c & 0x7) << 6;
    }
  }
  return mask;
}
#endif

// Convert HAL button booleans into the keypad bitmask the UI/event layer
// already expects, without reintroducing board-specific details above HAL.
uint16_t build_base_key_mask(const NullperatorHAL::Input::ButtonState_t& buttons) {
  uint16_t remapped = 0;
  remapped |= (buttons.left ? KEY_LEFT : 0u);
  remapped |= (buttons.down ? KEY_DOWN : 0u);
  remapped |= (buttons.right ? KEY_RIGHT : 0u);
  remapped |= (buttons.up ? KEY_UP : 0u);
  remapped |= (buttons.b ? KEY_EDIT : 0u);
  remapped |= (buttons.a ? KEY_ENTER : 0u);
  remapped |= (buttons.select ? KEY_ALT : 0u);
  return remapped;
}

// MENU long-press is a board-level power gesture, so centralize that timing
// here instead of scattering sleep checks through the input processing path.
void handle_menu_sleep(bool menu_pressed, uint32_t now_ms) {
  if (menu_pressed && (g_input_state.menu_pressed_since_ms == 0)) {
    g_input_state.menu_pressed_since_ms = now_ms;
    return;
  }

  if (!menu_pressed) {
    g_input_state.menu_pressed_since_ms = 0;
    return;
  }

  if ((now_ms - g_input_state.menu_pressed_since_ms) > SLEEP_HOLD_TIME_MS) {
    enter_sleep();
  }
}

// START has dual behavior: a short standalone tap emits PLAY, while a
// hold or chord behaves as NAV. Keep that state machine isolated so scanKeys()
// can read as a straight pipeline.
uint16_t resolve_start_key_mask(bool start_pressed, bool alt_pressed,
                                uint16_t other_keys, uint32_t now_ms) {
  StartButtonState& state = g_input_state.start;

  if (start_pressed) {
    if (!state.was_pressed) {
      state.started_with_alt = alt_pressed;
    }

    if (!state.tap_candidate && !state.nav_active) {
      if (other_keys == 0u) {
        state.tap_candidate = true;
        state.pressed_since_ms = now_ms;
      } else {
        state.nav_active = true;
      }
    }

    if (state.tap_candidate) {
      const bool chord_started = other_keys != 0u;
      const bool held_long_enough =
          (now_ms - state.pressed_since_ms) >= START_TAP_TIME_MS;
      if (chord_started || held_long_enough) {
        state.tap_candidate = false;
        state.nav_active = true;
      }
    }
  } else {
    const bool is_short_standalone_tap =
        state.tap_candidate &&
        (other_keys == 0u) &&
        ((now_ms - state.pressed_since_ms) < START_TAP_TIME_MS);
    if (is_short_standalone_tap) {
      state.play_pulse_pending = true;
    }

    state.tap_candidate = false;
    state.nav_active = false;
    state.started_with_alt = false;
  }

  state.was_pressed = start_pressed;

  uint16_t result = 0;
  if (state.nav_active) {
    result |= state.started_with_alt ? KEY_PLAY : KEY_NAV;
  }
  if (state.play_pulse_pending) {
    result |= KEY_PLAY;
    state.play_pulse_pending = false;
  }
  return result;
}

// Large one-frame mask jumps usually come from unstable reads during transitions.
// Keep the previous stable mask in that case so the UI does not see spurious chords.
uint16_t filter_unstable_changes(uint16_t key_mask) {
  if ((__builtin_popcount(key_mask ^ g_input_state.last_key_mask) > 3) &&
      (g_input_state.last_key_mask != kUninitializedKeyCache)) {
    return g_input_state.last_key_mask;
  }

  g_input_state.last_key_mask = key_mask;
  return key_mask;
}
}  // namespace

// Build the final keypad mask from remote UI or HAL button state, then apply
// the small amount of adapter-specific behavior that still lives above HAL.
uint16_t scanKeys() {
#ifdef USB_REMOTE_UI_INPUT
  const int16_t remote_mask = read_remote_ui_mask();
  if (remote_mask >= 0) {
    return static_cast<uint16_t>(remote_mask);
  }
#endif

  const NullperatorHAL::Input::ButtonState_t buttons =
      NullperatorHAL::Input::GetButtonState();
  const bool menu_pressed = buttons.func;
  const uint32_t now_ms = millis();
  uint16_t key_mask = build_base_key_mask(buttons);

  handle_menu_sleep(menu_pressed, now_ms);
  key_mask |= resolve_start_key_mask(buttons.start, buttons.select, key_mask,
                                     now_ms);

  return filter_unstable_changes(key_mask);
}
