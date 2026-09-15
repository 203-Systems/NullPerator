/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 nILS Podewski
 *
 * This file is part of the copingTracker firmware
 */

#pragma once

#include "../EnvelopeGenerators.h"
#include "Foundation/Types/Fixed.h"
#include "StackWavetables.generated.h"
#include <cstdint>

#include <algorithm>

#include "../ChiptuneInstrument/ChiptuneTables.h"
#include "StackEnums.h"

// Cent multipliers in Q16 fixed point (2^(cents/1200) * 65536)
#define CENT_MULT_0 65536   // 2^(0/1200)
#define CENT_MULT_25 66495  // 2^(25/1200)
#define CENT_MULT_50 67523  // 2^(50/1200)
#define CENT_MULT_M25 64648 // 2^(-25/1200)
#define CENT_MULT_M50 63686 // 2^(-50/1200)

/******************************************************************************
 * voice                                                                      *
 ******************************************************************************/

typedef struct stack_parameters_t {
  uint8_t spread;
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
  uint8_t volume;
  uint8_t brightness;
  uint8_t glide;
  uint8_t wave;
  int8_t transpose;
} stack_parameters_t;

typedef struct stack_pitch_envelope_t {
  int32_t value;
  int32_t rate;

  void trigger() {
    value = 0xffff;
  }

  int32_t tick() {
    value -= (static_cast<uint32_t>(value) * rate) >> 16;
    return value;
  }

  void set_rate(uint8_t inRate) {
    rate = (static_cast<uint16_t>(inRate) << 8) | inRate;
  }
} stack_pitch_envelope_t;

// (!) alignment has to be manually kept in this struct to allow using pack()
//     to keep the size as small as possible

typedef struct stack_voice_t {
  stack_parameters_t parameters; // parameters passed from instrument (10 bytes)

  uint8_t drive;    // unused currently
  uint8_t bitcrush; // bitcrush setting (only settable via command)

  uint32_t phase[5];         // oscillator phases
  int32_t frequency[5];      // precomp'd oscillator frequencies
  int32_t base_frequency[5]; // precomp'd oscillator frequencies
  uint8_t lut_index[5];      // index of mip table to use

  uint8_t volume;
  uint8_t level;
  uint8_t note;           // current base note
  stack_wave_type_e wave = stackWaveNone; // selected waveform

  adsr_envelope_t envelope; // volume envelope, size is 9 bytes
  uint8_t tock;             // sample counter for 1000Hz updates
  uint16_t tick;            // sample counter for 100Hz updates

  uint32_t time; // sample counter
  uint32_t timeToLive;

  stack_pitch_envelope_t pitch[5]; // pitch envelopes (8 bytes)

  stack_flags flags;
  int16_t notes[5];
  int8_t chord[4]{};
  int32_t previousFrequency = 0;
  int32_t pitchFactor = 65536, pitchTarget = 65536, pitchStep = 0;
  uint16_t pitchTicks = 0;
  uint16_t vibratoPhase = 0, vibratoRate = 0;
  uint8_t vibratoDepth = 0;
  uint8_t panPosition = 128, panTarget = 128;
  int16_t panStep = 0;
  uint8_t arpNotes[5]{};
  uint8_t arpLength = 1, arpIndex = 0;
  int32_t arpFactor = 65536;

  static int32_t ScaleFrequency(int32_t frequency, int32_t factor) {
    return static_cast<int32_t>(std::clamp<int64_t>(
        (int64_t(frequency) * factor) >> 16, 1, INT32_MAX));
  }
  void refresh_notes() {
    set_oscillator_note(0, note + parameters.transpose);
    for (int i = 1; i < stackNumOscillators; ++i)
      set_oscillator_note(i, note + parameters.transpose + chord[i - 1]);
  }
  void advance_arp() {
    if (arpLength > 1) {
      arpIndex = (arpIndex + 1) % arpLength;
      arpFactor = semitoneRatioQ16[128 + arpNotes[arpIndex]];
      update_frequency();
    }
  }
  void update_frequency() {
    const int32_t sine = interpolateS8(sine64LUT.data(), vibratoPhase >> 8);
    const int32_t vibratoFactor = 65536 +
        (int64_t(semitoneRatioQ16[129] - 65536) * sine * vibratoDepth) / (127 * 255);
    for (int o = 0; o < stackNumOscillators; ++o) {
      auto f = ScaleFrequency(base_frequency[o], pitch[o].value);
      f = ScaleFrequency(f, arpFactor);
      f = ScaleFrequency(f, pitchFactor);
      frequency[o] = ScaleFrequency(f, vibratoFactor);
    }
  }
  void command_arp(uint16_t value) {
    arpLength = 5;
    uint16_t tail = value;
    while (arpLength > 1 && (tail & 15) == 0) { --arpLength; tail >>= 4; }
    arpNotes[0] = 0;
    for (int i = 0; i < 4; ++i) arpNotes[i + 1] = (value >> (12 - i * 4)) & 15;
    arpIndex = 0;
    arpFactor = 65536;
    update_frequency();
  }
  void command_vibrato(uint8_t rate, uint8_t depth) {
    vibratoRate = uint16_t(rate) << 6;
    vibratoDepth = depth;
    vibratoPhase = 0;
    update_frequency();
  }
  void command_pan(uint8_t speed, uint8_t target) {
    panTarget = target;
    panStep = target > panPosition ? int(speed) : -int(speed);
    if (speed == 0) panPosition = target;
  }
  void slide_to(int32_t target, uint8_t duration) {
    pitchTarget = target;
    pitchTicks = duration;
    if (duration == 0) pitchFactor = target;
    else pitchStep = (pitchTarget - pitchFactor) / duration;
    update_frequency();
  }
  void command_pitch(uint8_t duration, int8_t semitones, bool legato) {
    if (legato && semitones == 0 && previousFrequency > 0) {
      pitchFactor = static_cast<int32_t>(std::clamp<int64_t>(
          (int64_t(previousFrequency) * 65536) / std::max(1, base_frequency[0]),
          1, INT32_MAX));
      slide_to(65536, duration);
    } else {
      slide_to(semitoneRatioQ16[128 + int(semitones)], duration);
    }
  }
  void command_finetune(uint8_t duration, int8_t amount) {
    const int32_t semitone = int32_t(semitoneRatioQ16[128 + sign(amount)]) - 65536;
    slide_to(65536 + semitone * std::abs(int(amount)) / 128, duration);
  }

  // implementation ------------------------------------------------------------

  inline uint32_t compute_cent_multiplier(int16_t cents) {
    if (cents == 0)
      return CENT_MULT_0;
    if (cents > 0) {
      if (cents <= 25) {
        return CENT_MULT_0 + ((CENT_MULT_25 - CENT_MULT_0) * cents) / 25;
      } else {
        return CENT_MULT_25 + ((CENT_MULT_50 - CENT_MULT_25) * (cents - 25)) / 25;
      }
    } else {
      cents = -cents;
      if (cents <= 25) {
        return CENT_MULT_0 - ((CENT_MULT_0 - CENT_MULT_M25) * cents) / 25;
      } else {
        return CENT_MULT_M25 - ((CENT_MULT_M25 - CENT_MULT_M50) * (cents - 25)) / 25;
      }
    }
  }

  inline void stop() {
    wave = stackWaveNone;
    level = 0;
    envelope.state = adsrIdle;
    for (int o = 0; o < stackNumOscillators; o++) {
      frequency[o] = 0;
      phase[o] = 0;
    }
  }

  inline void tick_100Hz() {
    // processing at ~100Hz

    // volume
    if (envelope.tick()) { stop(); return; }

    // recompute combined gain when envelope, pan or volume changes
    level = (uint32_t(parameters.volume) * volume * envelope.value) >> 24;

    // Pitch decay is independent of command pitch, arpeggio and vibrato.
    for (int o = 0; o < stackNumOscillators; o++) pitch[o].tick();
    if (pitchTicks > 0) {
      --pitchTicks;
      pitchFactor = pitchTicks == 0 ? pitchTarget : pitchFactor + pitchStep;
    }
    vibratoPhase += vibratoRate;
    if (panStep != 0) {
      const int next = int(panPosition) + panStep;
      if ((panStep > 0 && next >= panTarget) || (panStep < 0 && next <= panTarget)) {
        panPosition = panTarget;
        panStep = 0;
      } else {
        panPosition = static_cast<uint8_t>(std::clamp(next, 0, 255));
      }
    }
    update_frequency();
  }

  inline void tick_1000Hz() {
    if (timeToLive == 0) {
      if (flags.retrigger) {
        flags.retrigger = 0; // clear retrigger flag
        // retrigger without resetting clocks
        note_on(note, volume, false, parameters, true);
      } else {
        // note off, kill everything
        wave = stackWaveNone;
        envelope.state = adsrIdle;
        volume = 0;
      }
    } else {
      // length
      timeToLive--;
    }
  }

  inline void sample(fixed *left, fixed *right) {
    if (wave == stackWaveNone) { *left = *right = 0; return; }
    // precompute the gain, it doesn't need to be updated every sample

    // cold loop @ 100 Hz ------------------------------------------------------
    if (tick == 0) {
      tick = stackTicks100Hz;
      tick_100Hz();
    }

    // warm loop @ ~1000 Hz ----------------------------------------------------
    if (tock == 0) {
      tock = stackTicks1000Hz;
      tick_1000Hz(); // update at 1kHz for smoother pan and volume slides
    }

    // hot loop @ ~44100 Hz ----------------------------------------------------
    tick--;
    tock--;
    time++;

    for (int o = 0; o < stackNumOscillators; o++) {
      // advance phase
      phase[o] += frequency[o];
    }

    int32_t sample = 0;

    // generate sample based on waveform
    if (wave != stackWaveNone) {
      for (int o = 0; o < stackNumOscillators; o++) {
         // render wavetable
         sample += (StackWavetables::stack_wavetables[wave][lut_index[o]][phase[o] >> 21]);
      }
    }

    // sample is in the 17bits * 5 == +-0x27ffb range (18 bits), sample target volume is 31 bits
    // applying combined gain (volume * envelope) does not need any scaling back as it gets the level up to 26 bits
    sample *= level;

    // breng it up to 29 bits
    sample *= 8;

    // apply bitcrush
    if (bitcrush) {
      sample &= (0xffff'ffff << bitcrush);
      // drive is ignored at the moment
      // sample *= drive;
    }

    // Match Sample PAN: 00 right, FF left. Preserve the old centered output
    // bit-for-bit when no pan command is present.
    if (panPosition == 128) {
      *left = *right = sample;
    } else {
      *left = static_cast<int32_t>((int64_t(sample) * std::min(255, 2 * int(panPosition))) / 255);
      *right = static_cast<int32_t>((int64_t(sample) * std::min(255, 2 * (255 - int(panPosition)))) / 255);
    }
  }

  inline void set_oscillator_note(int osc, int note) {
    const int8_t cent_offsets[5] = {0, -1, 1, -2, 2};

    // clip note
    while (note < fLUT_MinNote) {
      note += 12;
    }
    while (note > fLUT_MaxNote) {
      note -= 12;
    }

    // apply spread (0-255) to cents (max ±25/±50 cents)
    int16_t cents = (cent_offsets[osc] * (int16_t)parameters.spread * 25) / 255;
    uint32_t multiplier = compute_cent_multiplier(cents);
    notes[osc] = note;
    // Detune can push a clamped high note past the signed phase-increment
    // range. Keep it below Nyquist before the pitch envelope multiplies it.
    base_frequency[osc] = static_cast<int32_t>(std::min<uint64_t>(
        (uint64_t(noteFrequency(note)) * multiplier) >> 16, INT32_MAX));
    frequency[osc] = base_frequency[osc];

    set_oscillator_lut_index(osc, note);
  }

  inline void set_oscillator_lut_index(int osc, int note) {
    // note must be within fLUT_MinNote..fLUT_MaxNote
    constexpr uint8_t noteRange = fLUT_MaxNote - fLUT_MinNote;
    const uint8_t notePos = note - fLUT_MinNote;
    const uint8_t mapped = (notePos * 6) / noteRange;
    const uint8_t brightness = parameters.brightness;

    // map the note to its LUT using brightness as a scale
    // Increasing brightness admits more harmonics. Keep the note-dependent
    // minimum mip level so high notes do not select the richest table.
    lut_index[osc] = 6 - ((6 - mapped) * brightness) / 12;
  }

  inline void note_on(unsigned char note, uint8_t inVolume, bool retrigger, const stack_parameters_t inParameters,
                      bool keepClocks = false) {
    previousFrequency = wave == stackWaveNone ? 0 : frequency[0];
    pitchFactor = pitchTarget = 65536;
    pitchStep = 0;
    pitchTicks = 0;
    vibratoPhase = vibratoRate = 0;
    vibratoDepth = 0;
    panPosition = panTarget = 128;
    panStep = 0;
    arpLength = 1;
    arpIndex = 0;
    arpFactor = 65536;
    std::fill_n(chord, 4, 0);
    parameters = inParameters;
    parameters.wave = std::min<uint8_t>(parameters.wave, stackWaveLastItem);
    parameters.brightness = std::min<uint8_t>(parameters.brightness, 12);
    const bool reset = retrigger || envelope.state == adsrIdle;

    // store volume
    volume = inVolume;
    level = 0xff;

    bitcrush = 0; // only accessible via command
    drive = 0;

    this->note = note;

    // oscillator frequency setup
    for (uint8_t o = 0; o < stackNumOscillators; o++) {
      set_oscillator_note(o, note + parameters.transpose);

      // reset oscillator phase
      if (reset) phase[o] = 0;
    }
    wave = (stack_wave_type_e)parameters.wave;

    timeToLive = -1;

    // don't reset timers on internal retrigger via command (IRT, ...)
    // they might be mid-execution and will underflow
    if (!keepClocks) {
      time = 0;
      tick = 0;
      tock = 0;
    }

    // reset envelope
    envelope.set_attack(parameters.attack);
    envelope.set_decay(parameters.decay);
    envelope.set_sustain(parameters.sustain);
    envelope.set_release(parameters.release);
    if (reset) envelope.trigger();

    // reset pitch envelope
    for (int o = 0; o < stackNumOscillators; o++) {
      pitch[o].set_rate(parameters.glide);
      pitch[o].trigger();
    }
  }

  /****************************************************************************
   * command processing                                                       *
   ****************************************************************************/

  void set_instrument_parameter(uint8_t param, uint8_t value) {
    
    switch (param) {
      case 0: // wave
        parameters.wave = std::min<uint8_t>(value, stackWaveLastItem);
        if (wave != stackWaveNone) wave = static_cast<stack_wave_type_e>(parameters.wave);
        break;
      case 1: // transpose
        parameters.transpose = static_cast<int8_t>(value);
        refresh_notes();
        update_frequency();
        break;
      case 2: // volume
        volume = value;
        break;
      case 3: // attack
        parameters.attack = value;
        envelope.set_attack(value);
        break;
      case 4: // decay
        parameters.decay = value;
        envelope.set_decay(value);
        break;
      case 5: // sustain
        parameters.sustain = value;
        envelope.set_sustain(value);
        break;
      case 6: // release
        parameters.release = value;
        envelope.set_release(value);
        break;
      case 7: // spread
        parameters.spread = value;
        refresh_notes();
        update_frequency();
        break;
      case 8: // brightness
        parameters.brightness = std::min<uint8_t>(value, 12);
        for (int o = 0; o < stackNumOscillators; o++) {
          set_oscillator_lut_index(o, notes[o]);
        }
        break;
      case 9: // glide
        parameters.glide = value;
        for (int o = 0; o < stackNumOscillators; o++) {
          pitch[o].set_rate(value);
        }
        break;
      default:
        // invalid parameter index, ignore for now
        break;
    }
  }

  void set_step_volume(uint8_t inVolume) {
    volume = inVolume;
  }

  void set_chord(int8_t a, int8_t b, int8_t c, int8_t d) {
    chord[0] = a; chord[1] = b; chord[2] = c; chord[3] = d;
    refresh_notes();
    update_frequency();
  }
} stack_voice_t;
