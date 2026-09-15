/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 */
#include "ChiptuneInstrument.h"
#include <algorithm>

ChiptuneInstrument::ChiptuneInstrument()
    : I_Instrument(&variables_),
      parameters_{
          Variable(FourCC::ChiptuneWave, coping::chip::chiptune_waveforms,
                   coping::chip::numWaveforms, coping::chip::defaultWaveform),
          Variable(FourCC::ChiptuneTranspose, 0),
          Variable(FourCC::ChiptuneVolume, 128),
          Variable(FourCC::ChiptuneBurst, VAR_OFF),
          Variable(FourCC::ChiptuneArpSpeed, 18),
          Variable(FourCC::ChiptuneLength, VAR_OFF),
          Variable(FourCC::ChiptuneAttack, 0),
          Variable(FourCC::ChiptuneDecay, 128),
          Variable(FourCC::ChiptuneVibratoDelay, 64),
          Variable(FourCC::ChiptuneVibratoDepth, 7),
          Variable(FourCC::ChiptuneSweepTime, 0),
          Variable(FourCC::ChiptuneSweepAmount, 0),
          Variable(FourCC::ChiptuneTable, VAR_OFF),
          Variable(FourCC::ChiptuneTableAuto, false)} {
  tableState_.Reset();
  for (auto &parameter : parameters_)
    variables_.push_back(&parameter);
}

void ChiptuneInstrument::OnStart() {
  tableState_.Reset();
  for (auto &voice : voices_)
    voice.stop();
}
void ChiptuneInstrument::Stop(int channel) {
  if (channel >= 0 && channel < SONG_CHANNEL_COUNT)
    voices_[channel].stop();
}
bool ChiptuneInstrument::Start(int channel, unsigned char note,
                               bool retrigger) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT || note > HIGHEST_NOTE)
    return false;
  const auto byte = [&](int i) {
    return std::clamp(parameters_[i].GetInt(), 0, 255);
  };
  coping::chip::InstrumentParameters params{};
  params.wave = static_cast<coping::chip::chiptune_wave_type_e>(
      std::clamp(parameters_[0].GetInt(), 0, coping::chip::numWaveforms - 1));
  params.transpose = std::clamp(parameters_[1].GetInt(), -24, 24);
  params.level = byte(2);
  params.burst = byte(3);
  params.arpSpeed = std::clamp(parameters_[4].GetInt(), 0, 34);
  params.length = byte(5);
  params.attack = byte(6);
  params.decay = byte(7);
  params.vibratoDelay = byte(8);
  params.vibratoDepth = byte(9);
  params.sweepTime = byte(10);
  params.sweepAmount = std::clamp(parameters_[11].GetInt(), -127, 127);
  voices_[channel].note_on(note, 255, retrigger, params);
  return true;
}
bool ChiptuneInstrument::Render(int channel, fixed *buffer, int size, bool) {
  if (!buffer || size <= 0 || channel < 0 || channel >= SONG_CHANNEL_COUNT)
    return false;
  auto &voice = voices_[channel];
  if (voice.wave == coping::chip::waveNone)
    return false;
  for (int i = 0; i < size; ++i)
    voice.sample(buffer + i * 2, buffer + i * 2 + 1);
  return true;
}
void ChiptuneInstrument::ProcessCommand(int channel, FourCC command,
                                        ushort value) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT)
    return;
  auto &voice = voices_[channel];
  const uint8_t hi = value >> 8, lo = value & 0xFF;
  switch (command) {
  case FourCC::InstrumentCommandSetInstrumentParameter:
    voice.set_instrument_parameter(hi, lo);
    break;
  case FourCC::InstrumentCommandArpeggiator:
    voice.command_init_arp(value);
    break;
  case FourCC::InstrumentCommandKill:
  case FourCC::InstrumentCommandGateOff:
    voice.stop();
    break;
  case FourCC::InstrumentCommandCrush:
    voice.bitcrush = lo & 15;
    voice.drive = hi;
    break;
  case FourCC::InstrumentCommandVibrato:
    voice.command_init_vibrato(hi, lo);
    break;
  case FourCC::InstrumentCommandPan:
    voice.command_init_pan(hi, lo);
    break;
  case FourCC::InstrumentCommandPitchSlide:
    voice.command_init_pitch_shift(hi, static_cast<int8_t>(lo));
    break;
  case FourCC::InstrumentCommandLegato:
    voice.command_init_legato(hi, static_cast<int8_t>(lo));
    break;
  case FourCC::InstrumentCommandVolume:
    voice.command_init_volume(hi, lo);
    break;
  case FourCC::InstrumentCommandPitchFineTune:
    voice.command_init_finetune(hi, static_cast<int8_t>(lo));
    break;
  default:
    break;
  }
}
