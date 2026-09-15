/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 nILS Podewski
 *
 * This file is part of the copingTracker firmware
 */

#pragma once

#include <cstdint>

namespace coping::chip {

enum chiptune_constants_e {
  envAttackThreshold = 65530,
  envDecayThreshold = 10,
  numWaveforms = 8,
  q16_16_1 = 0x0001'0000,
  ticks100Hz = 441,
  ticks1000Hz = 44,
  vibratoFrequency = 0xFFF,
};

inline constexpr const char *chiptune_waveforms[numWaveforms] = {
    "PULSE 12.5", "PULSE 25",  "PULSE 50",      "TRIANGLE 4BIT",
    "NOISE GB7",  "NOISE NES", "NOISE SN76489", "WHITE NOISE"};

enum chiptune_wave_type_e : uint8_t {
  wavePulse12_5 = 0,
  wavePulse25,
  wavePulse50,
  waveTriangle,
  waveNoiseGameBoy7,
  waveNoiseNES,
  waveNoiseSN76489,
  waveNoiseWhite,
  waveLastItem = waveNoiseWhite,
  waveNone
};

enum chiptune_env_state_e : uint8_t { envIdle, envAttack, envDecay };

enum chiptune_instrument_defaults_e {
  defaultArpSpeed = 0x12,
  defaultAttack = 0x00,
  defaultBurst = -1,
  defaultDecay = 0x80,
  defaultLength = -1,
  defaultLevel = 0x80,
  defaultSweepAmount = 0x00,
  defaultSweepTime = 0x00,
  defaultTable = -1,
  defaultTranspose = 0,
  defaultVibratoDelay = 0x40,
  defaultVibratoDepth = 0x07,
  defaultWaveform = wavePulse25
};

typedef union chiptuneFlags {
  struct {
    uint8_t arpeggio : 1;
    uint8_t legato : 1;
    uint8_t retrigger : 1;
    uint8_t volume : 1;
    uint8_t burstEnd : 1;
    uint8_t unused : 3;
  };
  uint8_t byte;
} chiptuneFlags;
} // namespace coping::chip
