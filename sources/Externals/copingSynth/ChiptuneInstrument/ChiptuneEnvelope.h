/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 nILS Podewski
 *
 * This file is part of the copingTracker firmware
 */

#pragma once

#include "ChiptuneEnums.h"
#include "ChiptuneMath.h"
#include "ChiptuneTables.h"

namespace coping::chip {
typedef struct envelope_t {
  uint16_t value;
  uint16_t coefficient; // q0.16
  uint16_t attack;
  uint16_t decay;
  chiptune_env_state_e state;

  void set_attack(uint8_t a) {
    // map 8 bit attack value to 16 bit coefficient using LUT and interpolation
    attack = interpolateU16(attackCoeffLUT.data(), a);
    if (state == envAttack)
      coefficient = attack;
  }

  void set_decay(uint8_t d) {
    // map 8 bit decay value to 16 bit coefficient using LUT and interpolation
    decay = interpolateU16(decayCoeffLUT.data(), d);
    if (state == envDecay)
      coefficient = decay;
  }

  void trigger() {
    coefficient = attack;
    state = envAttack;
    value = 0;
  }

  void tick() {
    if (state == envIdle)
      return;

    int32_t tmp;
    if (state == envAttack) {
      const uint32_t diff = 0xFFFFU - value;
      tmp = value + std::max<uint32_t>(1, (diff * coefficient) >> 16);
      if (tmp >= envAttackThreshold) {
        tmp = 0xFFFF;
        coefficient = decay;
        state = envDecay;
      }
    } else {
      // A signed decay and minimum decrement guarantee eventual silence.
      tmp =
          int32_t(value) -
          int32_t(std::max<uint32_t>(1, (uint32_t(value) * coefficient) >> 16));
      if (tmp <= envDecayThreshold) {
        tmp = 0;
        state = envIdle;
      }
    }

    value = tmp;
  }
} envelope_t;
} // namespace coping::chip