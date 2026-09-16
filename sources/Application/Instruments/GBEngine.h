/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once

#include "Externals/copingSynth/ChiptuneInstrument/ChiptuneTables.h"
#include "Foundation/Types/Fixed.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace gb {
enum class Kind : uint8_t { Wave, Pulse, Noise };

// Native tracker voices with GB period quantization, duty patterns, wave RAM,
// LFSR and frame-sequencer rates. Not a CPU/APU bus emulator: DMG wave-RAM
// corruption, envelope zombie writes and DIV-write quirks are not emulated.
// Reference: https://gbdev.io/pandocs/Audio_details.html
struct Voice {
  Kind kind = Kind::Pulse;
  bool active = false;
  uint8_t note = 60, volume = 128, pan = 128, panTarget = 128;
  int8_t transpose = 0;
  int16_t panStep = 0;
  uint8_t duty = 2, envelope = 0xF0, envelopeVolume = 15;
  uint8_t envelopeTimer = 0, sweep = 0, sweepTimer = 0;
  uint8_t noise = 0x35, waveLevel = 1;
  std::array<uint8_t, 16> wave{};
  uint16_t lfsr = 0x7FFF, length = 0, period = 0, sweepShadow = 0;
  uint8_t position = 0, frameStep = 0;
  uint32_t cycleFraction = 0, timer = 1, frameFraction = 0, fxFraction = 0;
  int32_t baseIncrement = 1, previousIncrement = 0;
  int32_t pitchFactor = 65536, pitchTarget = 65536, pitchStep = 0;
  uint8_t pitchTicks = 0, vibratoDepth = 0;
  uint16_t vibratoPhase = 0, vibratoRate = 0;
  std::array<uint8_t, 5> arp{};
  uint8_t arpLength = 1, arpIndex = 0;
  int32_t lastInput = 0, highPass = 0;

  void stop() { active = false; }
  static uint16_t PeriodForIncrement(Kind kind, int32_t increment) {
    const uint64_t numerator = uint64_t(kind == Kind::Wave ? 65536 : 131072)
                               << 32;
    const uint64_t denominator =
        uint64_t(std::max<int32_t>(1, increment)) * 44100;
    const auto divider = std::clamp<uint64_t>(
        (numerator + denominator / 2) / denominator, 1, 2048);
    return static_cast<uint16_t>(2048 - divider);
  }
  uint32_t timerPeriod() const {
    if (kind == Kind::Noise) {
      if ((noise >> 4) >= 14)
        return 0xFFFFFFFFU; // GB clocks stop here.
      const uint32_t divisor = (noise & 7) ? (noise & 7) * 16U : 8U;
      return divisor << (noise >> 4);
    }
    return (2048U - period) * (kind == Kind::Wave ? 2U : 4U);
  }
  void updatePitch() {
    if (kind == Kind::Noise)
      return;
    const int sine = sine64LUT[vibratoPhase >> 10];
    const int32_t vibrato =
        65536 + (int64_t(semitoneRatioQ16[129] - 65536) * sine * vibratoDepth) /
                    (127 * 255);
    auto inc = std::clamp<int64_t>((int64_t(baseIncrement) * pitchFactor) >> 16,
                                   1, INT32_MAX);
    inc = std::clamp<int64_t>(
        (inc * semitoneRatioQ16[128 + arp[arpIndex]]) >> 16, 1, INT32_MAX);
    inc = std::clamp<int64_t>((inc * vibrato) >> 16, 1, INT32_MAX);
    period = PeriodForIncrement(kind, static_cast<int32_t>(inc));
  }
  void trigger(uint8_t newNote, bool retrigger) {
    const bool reset = retrigger || !active;
    previousIncrement = active ? baseIncrement : 0;
    note = newNote;
    baseIncrement = noteFrequency(int(note) + transpose);
    pitchFactor = pitchTarget = 65536;
    pitchTicks = 0;
    vibratoPhase = vibratoRate = vibratoDepth = 0;
    arpLength = 1;
    arpIndex = 0;
    arp.fill(0);
    pan = panTarget = 128;
    panStep = 0;
    updatePitch();
    active = kind == Kind::Wave || (envelope & 0xF8) != 0;
    if (reset) {
      timer = timerPeriod();
      if (kind == Kind::Wave)
        position = 0;
      lfsr = 0x7FFF;
      envelopeVolume = envelope >> 4;
      envelopeTimer = (envelope & 7) ? envelope & 7 : 8;
      sweepShadow = period;
      sweepTimer = ((sweep >> 4) & 7) ? (sweep >> 4) & 7 : 8;
      if (kind == Kind::Pulse && (sweep & 7))
        checkSweep(false);
    }
  }
  void checkSweep(bool apply) {
    const int shift = sweep & 7;
    const int next = sweepShadow + ((sweep & 8) ? -(sweepShadow >> shift)
                                                : (sweepShadow >> shift));
    if (next > 2047) {
      stop();
      return;
    }
    if (apply && shift) {
      period = sweepShadow = static_cast<uint16_t>(next);
      checkSweep(false);
    }
  }
  void clockFrame() {
    if (!(frameStep & 1) && length && --length == 0)
      stop();
    if ((frameStep == 2 || frameStep == 6) && kind == Kind::Pulse &&
        --sweepTimer == 0) {
      sweepTimer = ((sweep >> 4) & 7) ? (sweep >> 4) & 7 : 8;
      if ((sweep >> 4) & 7)
        checkSweep(true);
    }
    if (frameStep == 7 && kind != Kind::Wave && (envelope & 7) &&
        --envelopeTimer == 0) {
      envelopeTimer = envelope & 7;
      const int next = envelopeVolume + ((envelope & 8) ? 1 : -1);
      if (next >= 0 && next <= 15)
        envelopeVolume = next;
    }
    frameStep = (frameStep + 1) & 7;
  }
  void clockFX() {
    bool pitchChanged = false;
    if (pitchTicks) {
      --pitchTicks;
      pitchFactor = pitchTicks ? pitchFactor + pitchStep : pitchTarget;
      pitchChanged = true;
    }
    if (vibratoRate && vibratoDepth) {
      vibratoPhase += vibratoRate;
      pitchChanged = true;
    }
    if (pitchChanged)
      updatePitch();
    if (panStep) {
      const int next = pan + panStep;
      if ((panStep > 0 && next >= panTarget) ||
          (panStep < 0 && next <= panTarget)) {
        pan = panTarget;
        panStep = 0;
      } else
        pan = static_cast<uint8_t>(std::clamp(next, 0, 255));
    }
  }
  void advanceArp() {
    if (arpLength > 1) {
      arpIndex = (arpIndex + 1) % arpLength;
      updatePitch();
    }
  }
  void setArp(uint16_t value) {
    arpLength = 5;
    uint16_t tail = value;
    while (arpLength > 1 && !(tail & 15)) {
      --arpLength;
      tail >>= 4;
    }
    arp[0] = 0;
    for (int i = 0; i < 4; ++i)
      arp[i + 1] = (value >> (12 - 4 * i)) & 15;
    arpIndex = 0;
    updatePitch();
  }
  void slide(int32_t target, uint8_t duration) {
    pitchTarget = target;
    pitchTicks = duration;
    if (duration)
      pitchStep = (pitchTarget - pitchFactor) / duration;
    else
      pitchFactor = target;
    updatePitch();
  }
  void setEnvelope(uint8_t value) {
    envelope = value;
    envelopeVolume = value >> 4;
    envelopeTimer = (value & 7) ? value & 7 : 8;
    if (!(value & 0xF8))
      stop();
  }
  int digitalSample() const {
    if (kind == Kind::Wave) {
      if (!waveLevel)
        return 0;
      const int nibble = (wave[position / 2] >> ((position & 1) ? 0 : 4)) & 15;
      // NR32 shifts the digital sample before the DAC; it is not an analog
      // volume multiplier. The output high-pass removes the resulting bias.
      return (nibble >> (waveLevel - 1)) * 2 - 15;
    }
    static constexpr uint8_t patterns[] = {0x01, 0x81, 0x87, 0x7E};
    const bool high =
        kind == Kind::Noise ? !(lfsr & 1) : ((patterns[duty] >> position) & 1);
    return high ? envelopeVolume : -int(envelopeVolume);
  }
  void edge() {
    if (kind == Kind::Noise) {
      const unsigned bit = (lfsr ^ (lfsr >> 1)) & 1;
      lfsr = (lfsr >> 1) | (bit << 14);
      if (noise & 8)
        lfsr = (lfsr & ~(1U << 6)) | (bit << 6);
    } else
      position = (position + 1) & (kind == Kind::Wave ? 31 : 7);
  }
  void sample(fixed *left, fixed *right) {
    *left = *right = 0;
    if (!active)
      return;
    frameFraction += 512;
    if (frameFraction >= 44100) {
      frameFraction -= 44100;
      clockFrame();
    }
    fxFraction += 100;
    if (fxFraction >= 44100) {
      fxFraction -= 44100;
      clockFX();
    }
    if (!active)
      return;
    // Integrate all oscillator edges within this output sample, including
    // high-frequency noise. Rendering is invariant to host buffer size.
    cycleFraction += 4194304;
    const uint32_t cycles = cycleFraction / 44100;
    cycleFraction %= 44100;
    uint32_t remaining = cycles;
    int integrated = 0;
    while (remaining) {
      const uint32_t span = std::min(remaining, timer);
      integrated += digitalSample() * int(span);
      remaining -= span;
      timer -= span;
      if (!timer) {
        edge();
        timer = timerPeriod();
      }
    }
    // Q8 averaging keeps the hot path's division 32-bit on ESP32. DC removal
    // is applied before native VOL so VOL 00 is immediately silent.
    const int32_t input = (integrated * 256) / int(cycles);
    highPass = input - lastInput + (highPass * 65280) / 65536;
    lastInput = input;
    const int32_t value =
        kind == Kind::Wave && !waveLevel
            ? 0
            : highPass * volume * 32;
    *left = static_cast<int32_t>(
        (int64_t(value) * std::min(255, 2 * int(pan))) / 255);
    *right = static_cast<int32_t>(
        (int64_t(value) * std::min(255, 2 * (255 - int(pan)))) / 255);
  }
};
} // namespace gb
