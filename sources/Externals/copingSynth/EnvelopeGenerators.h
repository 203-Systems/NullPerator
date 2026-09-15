/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 nILS Podewski
 *
 * This file is part of the copingTracker firmware
 */

#pragma once

#include <cstdint>
#include <algorithm>

inline constexpr int envAttackThreshold = 65530;
inline constexpr int envDecayThreshold = 10;
#include "ChiptuneInstrument/ChiptuneMath.h"
#include "ChiptuneInstrument/ChiptuneTables.h"

enum adsr_env_state_e : uint8_t { adsrIdle, adsrAttack, adsrDecay, adsrSustain, adsrRelease };

typedef struct adsr_envelope_t {
  uint16_t value;       // q0.16
  uint16_t coefficient; // q0.16
  uint16_t attack;
  uint16_t decay;
  uint16_t sustain;
  uint16_t release;
  adsr_env_state_e state;

  void set_attack(uint8_t a) {
    // map 8 bit attack value to 16 bit coefficient using LUT and interpolation
    attack = interpolateU16(attackCoeffLUT.data(), a);
    if (state == adsrAttack) coefficient = attack;
  }

  void set_decay(uint8_t d) {
    // map 8 bit decay value to 16 bit coefficient using LUT and interpolation
    decay = interpolateU16(decayCoeffLUT.data(), d) >> 2;
    if (state == adsrDecay) coefficient = decay;
  }

  void set_sustain(uint8_t s) {
    // map 8 bit sustain level (0-255) to 16 bit value (0-65535)
    sustain = s << 8;
    if (state == adsrSustain) value = sustain;
  }

  void set_release(uint8_t r) {
    // map 8 bit release value to 16 bit coefficient using LUT and interpolation
    release = interpolateU16(decayCoeffLUT.data(), r);
    if (state == adsrRelease) coefficient = release;
  }

  void trigger() {
    coefficient = attack;
    state = adsrAttack;
    value = 0;
  }

  void release_note() {
    if (state == adsrAttack || state == adsrDecay || state == adsrSustain) {
      coefficient = release;
      state = adsrRelease;
    }
  }

  // returns true if envelope is idle (finished)
  bool tick() {
    if (state == adsrIdle) {
      return true;
    }

    uint32_t diff;
    int32_t tmp;

    switch (state) {
      case adsrAttack:
        diff = 0xFFFF - value;
        tmp = value + std::max<uint32_t>(1, (diff * coefficient) >> 16);
        if (tmp >= envAttackThreshold) {
          tmp = 0xFFFF;
          coefficient = decay;
          state = adsrDecay;
        }
        break;

      case adsrDecay:
        diff = value; // decay from 0xFFFF down to sustain level
        tmp = int32_t(value) - int32_t(std::max<uint32_t>(1, (diff * coefficient) >> 16));
        if (tmp <= sustain) {
          tmp = sustain;
          state = adsrSustain;
        }
        break;

      case adsrSustain:
        // hold at sustain level until release_note() is called
        return false;

      case adsrRelease:
        diff = value; // release from current level down to 0
        tmp = int32_t(value) - int32_t(std::max<uint32_t>(1, (diff * coefficient) >> 16));
        if (tmp <= envDecayThreshold) {
          tmp = 0;
          state = adsrIdle;
        }
        break;

      default:
        return true;
    }

    value = tmp;
    return (value == 0 && state == adsrIdle);
  }
} adsr_envelope_t;
