/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Foundation/Types/Fixed.h"
#include <cstdint>

// Only called at a ping-pong boundary. Fold the entire overshoot in constant
// time, even when one output frame crosses a short loop many times. The
// per-sample advance remains 32-bit; wide division stays off the common path.
inline void ReflectSamplePosition(int &frame, fixed &fraction, bool &reverse,
                                  int low, int high) {
  if (high <= low) {
    frame = low;
    fraction = 0;
    reverse = false;
    return;
  }
  const std::int64_t span = std::int64_t(high - low) * FP_ONE;
  const std::int64_t period = span * 2;
  std::int64_t phase =
      ((std::int64_t(frame) - low) * FP_ONE + fraction) % period;
  if (phase < 0)
    phase += period;
  if (phase > span) {
    phase = period - phase;
    reverse = !reverse;
  }
  if (phase == 0)
    reverse = false;
  else if (phase == span)
    reverse = true;
  frame = low + static_cast<int>(phase >> FIXED_SHIFT);
  fraction = static_cast<fixed>(phase & (FP_ONE - 1));
}
