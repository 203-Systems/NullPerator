/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 */
#include "ChiptuneInstrument.h"
#include <algorithm>

namespace {
const Variable::Descriptor kChiptuneParameters[] = {
    {FourCC::ChiptuneWave, coping::chip::chiptune_waveforms,
     coping::chip::numWaveforms, coping::chip::defaultWaveform},
    {FourCC::ChiptuneTranspose, 0},
    {FourCC::ChiptuneVolume, 128},
    {FourCC::ChiptuneBurst, VAR_OFF},
    {FourCC::ChiptuneArpSpeed, 18},
    {FourCC::ChiptuneLength, VAR_OFF},
    {FourCC::ChiptuneAttack, 0},
    {FourCC::ChiptuneDecay, 128},
    {FourCC::ChiptuneVibratoDelay, 64},
    {FourCC::ChiptuneVibratoDepth, 7},
    {FourCC::ChiptuneSweepTime, 0},
    {FourCC::ChiptuneSweepAmount, 0},
    {FourCC::ChiptuneTable, VAR_OFF},
    {FourCC::ChiptuneTableAuto, false},
};
} // namespace

ChiptuneInstrument::ChiptuneInstrument(
    TrackVoicePool<coping::chip::voice_t> *voices)
    : I_Instrument(&variables_),
      parameters_{
          Variable(kChiptuneParameters[0]),  Variable(kChiptuneParameters[1]),
          Variable(kChiptuneParameters[2]),  Variable(kChiptuneParameters[3]),
          Variable(kChiptuneParameters[4]),  Variable(kChiptuneParameters[5]),
          Variable(kChiptuneParameters[6]),  Variable(kChiptuneParameters[7]),
          Variable(kChiptuneParameters[8]),  Variable(kChiptuneParameters[9]),
          Variable(kChiptuneParameters[10]), Variable(kChiptuneParameters[11]),
          Variable(kChiptuneParameters[12]), Variable(kChiptuneParameters[13])},
      voices_(voices) {
  tableState_.Reset();
  for (auto &parameter : parameters_)
    variables_.push_back(&parameter);
}

void ChiptuneInstrument::OnStart() {
  tableState_.Reset();
  voices_.Reset();
}
void ChiptuneInstrument::Stop(int channel) {
  if (channel >= 0 && channel < SONG_CHANNEL_COUNT)
    voices_.Stop(channel);
}
bool ChiptuneInstrument::Start(int channel, unsigned char note,
                               bool retrigger) {
  if (!voices_.IsValid() || channel < 0 || channel >= SONG_CHANNEL_COUNT ||
      note > HIGHEST_NOTE)
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
  voices_.Acquire(channel).note_on(note, 255, retrigger, params);
  return true;
}
bool ChiptuneInstrument::Render(int channel, fixed *buffer, int size, bool) {
  if (!buffer || size <= 0 || !voices_.Owns(channel))
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
  if (!voices_.Owns(channel))
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
