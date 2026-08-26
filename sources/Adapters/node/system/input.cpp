#include "input.h"
#include "Adapters/node/hal/nullperator/input/input.h"
#include "Adapters/node/platform/platform.h"

namespace {
constexpr uint16_t kUninitializedKeyCache = 0xFFFF;

struct InputState {
  uint16_t last_key_mask = kUninitializedKeyCache;
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
  // Raw Node labels are translated to the M8 semantic layout by the shared
  // dispatcher: B=OPTION, A=EDIT, SELECT=SHIFT, START=PLAY.
  remapped |= (buttons.b ? KEY_EDIT : 0u);
  remapped |= (buttons.a ? KEY_ENTER : 0u);
  remapped |= (buttons.select ? KEY_ALT : 0u);
  remapped |= (buttons.start ? KEY_NAV : 0u);
  remapped |= (buttons.func ? KEY_POWER : 0u);
  return remapped;
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
uint16_t scanKeys(bool *headphoneConnected) {
  const NullperatorHAL::Input::ButtonState_t buttons =
      NullperatorHAL::Input::GetButtonState(headphoneConnected);

#ifdef USB_REMOTE_UI_INPUT
  const int16_t remote_mask = read_remote_ui_mask();
  if (remote_mask >= 0) {
    return static_cast<uint16_t>(remote_mask);
  }
#endif

  return filter_unstable_changes(build_base_key_mask(buttons));
}
