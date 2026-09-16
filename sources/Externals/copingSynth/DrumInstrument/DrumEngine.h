/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 nILS Podewski
 *
 * This file is part of the copingTracker firmware
 */

#pragma once

#include "Foundation/Types/Fixed.h"
#include "Foundation/Types/SynthLevel.h"
#include <cstdint>

#include <algorithm>

#include "../ChiptuneInstrument/ChiptuneTables.h"
#include "DrumEnums.h"
#include "DrumEnvelope.h"

#define SAMPLE_LEVEL 0x0FFF'FFFF
#define HALF_SAMPLE_LEVEL (SAMPLE_LEVEL >> 1)
#define NOISE_PHASE_LENGTH 0x4000'0000

/******************************************************************************
 * voice                                                                      *
 ******************************************************************************/

typedef struct drum_parameters_t {
  uint8_t wave : 4;
  uint8_t decay : 4;
  uint8_t note : 4;
  uint8_t pitch : 4;
  uint8_t character : 8;
  uint8_t padding : 8;
} drum_parameters_t;

typedef struct pitch_envelope_t {
  int32_t value;
  int32_t rate;

  void trigger() {
    value = 0xffff;
  }

  void tick() {
    value -= (static_cast<uint32_t>(value) * rate) >> 16;
  }

  void set_rate(uint8_t inRate) {
    rate = (static_cast<uint16_t>(inRate) << 8) | inRate;
  }

} pitch_envelope_t;

static_assert(sizeof(drum_parameters_t) == 4, "Check sizeof(drum_parameters_t) in error message");

// (!) alignment has to be manually kept in this struct to allow using pack()
//     to keep the size as small as possible

typedef struct drum_voice_t {
  drum_parameters_t parameters; // parameters passed from instrument

  uint32_t phase = 0;         // oscillator phase
  int32_t frequency = 0;      // precomp'd oscillator frequency
  int32_t base_frequency = 0; // precomp'd oscillator frequency
  uint32_t lastSample = 0;    // used for both the last sample for pulse smoothing
                              // and as the lcg register for the noise

  uint8_t drive;    // unused currently
  uint8_t bitcrush; // bitcrush setting (only settable via command)

  uint8_t note;          // current base note
  drum_wave_type_e wave = drumWaveNone; // selected waveform

  uint16_t lfsr = 17; // shift register for the noise generators

  drum_envelope_t envelope; // volume envelope, size is 9 bytes
  pitch_envelope_t pitch;   // pitch envelope

  uint16_t tick; // sample counter for 100Hz updates
  uint32_t time; // sample counter
  uint8_t tock;  // sample counter for 1000Hz updates

  uint8_t volume;
  uint8_t level;
  uint32_t timeToLive;

  drum_flags flags;

  // character settings
  uint8_t glitch_trigger_delay;
  uint16_t glitch = 17; // shift register for the glitch randomizer

  // implementation ------------------------------------------------------------

  inline uint16_t get_glitch() {
    // The LCG deliberately wraps; integer promotion to signed int would make
    // this overflow undefined once character modulation advances the seed.
    glitch = static_cast<uint16_t>(uint32_t(glitch) * 1664525U + 1013904223U);
    return glitch;
  }

  inline void stop() {
    wave = drumWaveNone;
    envelope.state = drumEnvIdle;
    level = 0;
    frequency = 0;
    phase = 0;
  }

  inline void tick_100Hz() {
    // processing at ~100Hz

    // volume
    envelope.tick();
    if (envelope.state == drumEnvIdle) { stop(); return; }

    // recompute combined gain when envelope, pan or volume changes
    level = (volume * envelope.value) >> 16;

    // pitch
    pitch.tick();
    frequency = uint32_t((uint64_t(base_frequency) * pitch.value) >> 16);
  }

  inline void tick_1000Hz() {
    if (timeToLive == 0) {
      if (flags.retrigger) {
        flags.retrigger = 0; // clear retrigger flag
        // retrigger without resetting clocks
        note_on(note, volume, false, parameters, true);
      } else {
        // note off, kill everything
        wave = drumWaveNone;
        envelope.state = drumEnvIdle;
        volume = 0;
      }
    } else {
      // length
      timeToLive--;
    }
  }

  inline void sample(fixed *left, fixed *right) {
    if (wave == drumWaveNone) { *left = *right = 0; return; }
    // precompute the gain, it doesn't need to be updated every sample

    // cold loop @ 100 Hz ------------------------------------------------------
    if (tick == 0) {
      tick = drumTicks100Hz;
      tick_100Hz();
    }

    // warm loop @ ~1000 Hz ----------------------------------------------------
    if (tock == 0) {
      tock = drumTicks1000Hz;
      tick_1000Hz(); // update at 1kHz for smoother pan and volume slides
    }

    // hot loop @ ~44100 Hz ----------------------------------------------------
    tick--;
    tock--;
    time++;

    // advance phase
    phase += frequency;

    uint32_t sample = 0;

    // generate sample based on waveform
    switch (wave) {
      case drumWavePulse12_5: // pulse 12.5%
        sample = pulse(phase > 0x2000'0000);
        break;
      case drumWavePulse25: // pulse 25%
        sample = pulse(phase > 0x4000'0000);
        break;
      case drumWavePulse50: // pulse 50%
        sample = pulse(phase > 0x8000'0000);
        break;
      case drumWaveTriangle: // triangle
        if (phase < 0x8000'0000) {
          // first half, rising slope
          sample = phase >> 3;
        } else {
          // second half, falling slope
          sample = (0xFFFF'FFFF - phase) >> 3;
        }
        sample &= 0xFF00'0000; // downsample
        break;
      case drumWaveNoiseGameBoy7: // noise: GB7
        sample = voice_noise_lfsr(1, 6);
        break;
      case drumWaveNoiseNES: // noise: NES
        sample = voice_noise_lfsr(6, 14);
        break;
      case drumWaveNoiseSN76489: // noise: SN76489
        sample = voice_noise_lfsr(3, 14);
        break;
      case drumWaveNoiseWhite:                            // noise: white noise
        lastSample = (lastSample * 1664525) + 1013904223; // frequency independent
        sample = lastSample & SAMPLE_LEVEL;
        break;
      case drumWaveNone:
        sample = 0;
        break;
    }

    if (glitch_trigger_delay) {
      glitch_trigger_delay--;
      sample = 0;
    }

    // apply combined gain (volume * envelope) in single operation
    sample = (sample >> 8) * level;

    // apply bitcrush
    if (bitcrush) {
      sample &= (0xffff'ffff << bitcrush);
      // drive is ignored at the moment
      // sample *= drive;
    }

    // apply panning
    *left = *right = SynthLevel::Coping(
        int32_t(sample) - int32_t((HALF_SAMPLE_LEVEL >> 8) * level));
  }

  inline void note_on(unsigned char note, uint8_t inVolume, bool retrigger, const drum_parameters_t inParameters,
                      bool keepClocks = false) {
    // bool retrigger is currently unused
    parameters = inParameters;

    // store volume
    volume = inVolume;

    bitcrush = 0; // only accessible via command
    drive = 0;

    // oscillator frequency setup
    int fIndex = 64 + 3 * parameters.note; // 64..109
    base_frequency = noteFrequency(fIndex - 12); // Preserve upstream drum tuning.
    frequency = base_frequency;
    wave = (drum_wave_type_e)(parameters.wave % drumNumWaveforms);

    // reset noise seed to get deterministic noise
    lfsr = 42;
    lastSample = 42;

    this->note = note;

    // reset oscillator state and timers
    phase = 0;
    timeToLive = -1;

    // don't reset timers on internal retrigger via command (IRT, ...)
    // they might be mid-execution and will underflow
    if (!keepClocks) {
      time = 0;
      tick = 0;
      tock = 0;
    }

    // reset envelope
    envelope.set_decay(parameters.decay << 4);
    envelope.trigger();

    // reset pitch envelope
    pitch.set_rate(parameters.pitch << 4);
    pitch.trigger();

    glitch_trigger_delay = 0;
    // glitch/character settings
    if (parameters.character) {
      // delay
      glitch_trigger_delay = get_glitch() % (parameters.character << 4);

      // pitch
      uint32_t amount = (get_glitch() & 0xfff) * parameters.character;
      frequency += (amount - 524287);

      // volume
      amount = (get_glitch() & 0xff) * parameters.character;
      envelope.value -= amount;
    }
  }

  /****************************************************************************
   *  waveform generation                                                     *
   ****************************************************************************/

  // simple slew limited pulse generator to avoid some of the aliasing noise
  // while keeping the implementation fast enough for 8 voices on rp2040
  inline uint32_t pulse(bool high) {
    int32_t target = high ? SAMPLE_LEVEL : 0;
    int32_t step = int32_t(std::min<int64_t>(INT32_MAX, int64_t(frequency) + 0x1FFF'FFFF)); // 0.125 + phase increment
    int32_t diff = std::clamp(target - (int32_t)lastSample, -step, step);
    return (lastSample = (lastSample + diff));
  }

  // LFSR function for noise generation, parameterized by tap and feedback bits
  inline uint32_t voice_noise_lfsr(uint8_t bit, uint8_t feedback) {
    // only resample noise when the phase is > 25% to get frequency-dependent
    // noise
    if (phase > NOISE_PHASE_LENGTH) {
      phase -= NOISE_PHASE_LENGTH;

      uint32_t bitA = lfsr & 1;
      uint32_t bitB = (lfsr >> bit) & 1;
      uint32_t bitF = bitA ^ bitB;

      lfsr = (lfsr >> 1) | (bitF << feedback);
      lastSample = bitA ? 0x0FFF'FFFF : 0;
    }

    return lastSample;
  }

  /****************************************************************************
   *  command processing                                                     *
   ****************************************************************************/

  void set_instrument_parameter(uint8_t param, uint8_t value) {
    
    switch (param) {
      default:
        // invalid parameter index, ignore for now
        break;
    }
  }

  void set_step_volume(uint8_t inVolume) {
    volume = inVolume;
  }
} drum_voice_t;
