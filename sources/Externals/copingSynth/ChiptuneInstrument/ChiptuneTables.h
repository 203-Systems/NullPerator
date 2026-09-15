/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 nILS Podewski
 *
 * This file is part of the copingTracker firmware
 */

#pragma once

#include "ChiptuneCompileTimeFunctions.h"
#include <algorithm>

// precalculated semitone ratios for pitch slides (Q16.16 format)
inline constexpr auto semitoneRatioQ16 = gen_semi_lut(std::make_index_sequence<256>{});

// precalculated frequency table midi notes -12 to 127+12
#define fLUT_MinNote -12
#define fLUT_MaxNote 138
constexpr auto frequencyLUT = gen_frq_lut(std::make_index_sequence<128 + 24>{});
// precalculated attack coefficients for envelope (0-64)
constexpr auto attackCoeffLUT = gen_attack_lut(std::make_index_sequence<65>{});
// precalculated decay coefficients for envelope (0-64)
constexpr auto decayCoeffLUT = gen_decay_lut(std::make_index_sequence<65>{});
// precalculated sine wave values for vibrato (0-63 + sentinel)
inline constexpr auto sine64LUT = gen_sine64_lut(std::make_index_sequence<65>{});


// Table element zero represents MIDI -12. Always offset signed note indices.
inline int32_t noteFrequency(int note) {
  return frequencyLUT[static_cast<unsigned>(std::clamp(note, -12, 138) + 12)];
}
