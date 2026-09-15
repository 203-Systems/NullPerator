#ifdef NDEBUG
#undef NDEBUG // DSP checks must run in Release host builds too.
#endif
#include "Externals/copingSynth/ChiptuneInstrument/ChiptuneEngine.h"
#include "Externals/copingSynth/DrumInstrument/DrumEngine.h"
#include "Externals/copingSynth/StackInstrument/StackEngine.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

static void TestChiptune() {
  using namespace coping::chip;
  InstrumentParameters params{};
  params.level = 255;
  params.decay = 128;
  params.arpSpeed = 34;
  for (int wave = 0; wave < numWaveforms; ++wave) {
    voice_t voice{};
    params.wave = static_cast<chiptune_wave_type_e>(wave);
    voice.note_on(60, 255, true, params);
    fixed left = 0, right = 0;
    bool positive = false, negative = false;
    for (int i = 0; i < 4410; ++i) {
      voice.sample(&left, &right);
      positive |= left > 0;
      negative |= left < 0;
      assert(left == right);
    }
    assert(positive && negative);
    voice.stop();
    voice.set_instrument_parameter(0, 3);
    voice.sample(&left, &right);
    assert(left == 0 && right == 0);
  }
  params.wave = wavePulse50;
  voice_t voice{};
  voice.note_on(69, 255, true, params);
  voice.command_init_arp(0x4700);
  assert(voice.arp.length == 3);
  voice.tick_1000Hz();
  assert(voice.arp.index == 1);
  assert(voice.frequency == noteFrequency(73));
  voice.command_init_arp(0);
  assert(voice.arp.length == 1 && voice.frequency == noteFrequency(69));
  voice.command_init_pitch_shift(0, 12);
  assert(std::abs(int64_t(voice.frequency) - int64_t(noteFrequency(69)) * 2) <=
         1);
  voice.command_init_pitch_shift(2, -12);
  voice.tick_100Hz();
  voice.tick_100Hz();
  assert(voice.legato.steps == 0);
  assert(voice.frequency == noteFrequency(69) / 2);
  voice.command_init_finetune(0, 0);
  assert(voice.frequency == noteFrequency(69));
  voice.command_init_vibrato(64, 255);
  voice.time = 100;
  voice.tick_100Hz();
  assert(voice.frequency != noteFrequency(69));
  voice.command_init_vibrato(0, 0);
  assert(voice.frequency == noteFrequency(69));
  voice.command_init_pan(0, 0);
  assert(voice.gain.left == 0 && voice.gain.right == 255);
  voice.command_init_pan(255, 255);
  voice.tick_100Hz();
  assert(voice.pan.position == 255 && voice.gain.left == 255 &&
         voice.gain.right == 0);
  voice.command_init_volume(0, 0);
  fixed left = 1, right = 1;
  voice.sample(&left, &right);
  assert(left == 0 && right == 0);
  voice.command_init_volume(1, 255);
  voice.tick_100Hz();
  assert(voice.volume.level == 255);
  voice.note_on(72, 255, false, params);
  const auto phase = voice.phase;
  voice.command_init_legato(2, 0);
  voice.tick_100Hz();
  voice.tick_100Hz();
  assert(voice.legato.factor == q16_16_1 && voice.phase == phase);

  // Full byte input range: SIP clamps lookup indices, modulation cannot
  // overflow.
  for (int value = 0; value < 256; ++value) {
    voice.note_on(value % 120, 255, true, params);
    voice.command_init_arp(0xFFFF);
    voice.command_init_pitch_shift(value, static_cast<int8_t>(value));
    voice.command_init_pan(value, 255 - value);
    voice.command_init_vibrato(value, value);
    for (int parameter = 0; parameter < 12; ++parameter) {
      voice.set_instrument_parameter(parameter, value);
      for (int i = 0; i < 64; ++i)
        voice.sample(&left, &right);
      assert(voice.frequency >= 0);
    }
  }
  for (int coefficient : {0, 1, 128, 255}) {
    envelope_t env{};
    env.set_attack(coefficient);
    env.set_decay(coefficient);
    env.trigger();
    for (int tick = 0; tick < 140000 && env.state != envIdle; ++tick)
      env.tick();
    assert(env.state == envIdle && env.value == 0);
  }
}

static void TestStackModulation() {
  stack_parameters_t params{};
  params.wave = stackWaveSaw;
  params.volume = 128;
  params.sustain = 255;
  stack_voice_t voice{};
  voice.note_on(60, 255, true, params);
  voice.set_chord(-8, -1, 4, 7);
  assert(voice.notes[1] == 52 && voice.notes[2] == 59 && voice.notes[4] == 67);
  voice.set_instrument_parameter(1, 12);
  assert(voice.notes[0] == 72 && voice.notes[1] == 64 && voice.notes[4] == 79);
  voice.command_arp(0x4700);
  voice.advance_arp();
  assert(voice.arpLength == 3 && voice.arpIndex == 1);
  assert(voice.frequency[0] > voice.base_frequency[0]);
  voice.command_vibrato(64, 255);
  const auto before = voice.frequency[0];
  voice.tick_100Hz();
  assert(voice.frequency[0] != before);
  voice.command_vibrato(0, 0);
  voice.command_arp(0);
  voice.command_pitch(2, 12, false);
  voice.tick_100Hz();
  voice.tick_100Hz();
  assert(voice.pitchFactor == 131072);
  voice.command_finetune(0, 0);
  assert(voice.pitchFactor == 65536);
  voice.command_pan(0, 0);
  fixed left = 1, right = 1;
  bool audible = false;
  for (int i = 0; i < 500; ++i) {
    voice.sample(&left, &right);
    assert(left == 0);
    audible |= right != 0;
  }
  assert(audible);
  voice.command_pan(255, 255);
  voice.tick_100Hz();
  assert(voice.panPosition == 255);
  for (int value = 0; value < 256; ++value) {
    voice.note_on(value % 120, 255, true, params);
    voice.command_arp(0xFFFF);
    voice.advance_arp();
    voice.command_pitch(value, static_cast<int8_t>(value), value % 2);
    voice.command_vibrato(value, value);
    voice.command_pan(value, value);
    for (int parameter = 0; parameter < 10; ++parameter) {
      voice.set_instrument_parameter(parameter, value);
      for (int i = 0; i < 64; ++i)
        voice.sample(&left, &right);
    }
  }
}

int main() {
  TestChiptune();
  TestStackModulation();
  // MIDI A4 phase increment at 44.1 kHz; catch table-origin and truncation bugs.
  assert(std::abs(double(noteFrequency(69)) * 44100.0 / 4294967296.0 - 440) < .01);
  for (int wave = 0; wave < drumNumWaveforms; ++wave) {
    drum_voice_t voice{};
    fixed left = 1, right = 1;
    voice.sample(&left, &right);
    assert(left == 0 && right == 0);
    drum_parameters_t params{};
    params.wave = wave;
    params.decay = 8;
    params.note = 8;
    params.pitch = 4;
    voice.note_on(60, 255, true, params);
    bool audible = false;
    for (int i = 0; i < 44100; ++i) {
      voice.sample(&left, &right);
      audible |= left != 0;
      assert(left == right);
    }
    assert(audible);
    voice.stop();
    for (int i = 0; i < 512; ++i) {
      voice.sample(&left, &right);
      assert(left == 0 && right == 0);
    }
  }
  for (int wave = 0; wave < stackNumWaveforms; ++wave) {
    for (int note : {0, 12, 69, 119}) {
      stack_voice_t voice{};
      stack_parameters_t params{};
      params.wave = wave;
      params.volume = 128;
      params.sustain = 255;
      params.brightness = 7;
      params.transpose = -24;
      params.spread = 255;
      voice.note_on(note, 255, true, params);
      voice.set_chord(-12, 4, 7, 12);
      fixed left = 0, right = 0;
      bool audible = false;
      for (int i = 0; i < 4096; ++i) {
        voice.sample(&left, &right);
        audible |= left != 0;
        assert(left == right);
        assert(std::abs(int64_t(left)) < INT32_MAX);
      }
      assert(audible);
      voice.envelope.release_note();
      for (int i = 0; i < 44100; ++i) voice.sample(&left, &right);
      assert(voice.wave == stackWaveNone);
      assert(left == 0 && right == 0);
    }
  }
  stack_parameters_t params{};
  params.wave = stackWaveSaw;
  params.volume = 128;
  params.sustain = 255;
  stack_voice_t dark{}, bright{};
  params.brightness = 12;
  bright.note_on(69, 255, true, params);
  params.brightness = 0;
  dark.note_on(69, 255, true, params);
  bool differs = false;
  for (int i = 0; i < 4096; ++i) {
    fixed a, b, unused;
    bright.sample(&a, &unused);
    dark.sample(&b, &unused);
    differs |= a != b;
  }
  assert(differs);
  params.transpose = 24;
  params.spread = 255;
  bright.note_on(119, 255, true, params);
  bright.set_chord(15, 15, 15, 15);
  for (int oscillator = 0; oscillator < stackNumOscillators; ++oscillator)
    assert(bright.base_frequency[oscillator] > 0);
  std::cout << "Drum, Stack, and Chiptune DSP tests passed; voice bytes: "
            << sizeof(drum_voice_t) << ", " << sizeof(stack_voice_t) << ", "
            << sizeof(coping::chip::voice_t) << '\n';
}
