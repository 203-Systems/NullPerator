/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 */
#include "StackInstrument.h"
#include <algorithm>

StackInstrument::StackInstrument()
    : I_Instrument(&variables_),
      parameters_{Variable(FourCC::StackSpread, 0),
                  Variable(FourCC::StackWave, stackWaveNames, stackNumWaveforms,
                           stackWaveSaw),
                  Variable(FourCC::StackTranspose, 0),
                  Variable(FourCC::StackTable, VAR_OFF),
                  Variable(FourCC::StackTableAuto, false),
                  Variable(FourCC::StackAttack, 0),
                  Variable(FourCC::StackDecay, 0),
                  Variable(FourCC::StackSustain, 255),
                  Variable(FourCC::StackRelease, 0),
                  Variable(FourCC::StackVolume, 128),
                  Variable(FourCC::StackBrightness, 12),
                  Variable(FourCC::StackGlide, 0),
                  Variable(FourCC::StackChord, 0x047C)} {
  tableState_.Reset();
  for (auto &parameter : parameters_)
    variables_.push_back(&parameter);
}

void StackInstrument::OnStart() {
  tableState_.Reset();
  for (auto &voice : voices_)
    voice.stop();
}

void StackInstrument::Stop(int channel) {
  if (channel >= 0 && channel < SONG_CHANNEL_COUNT)
    voices_[channel].stop();
}

bool StackInstrument::Start(int channel, unsigned char note, bool retrigger) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT || note > HIGHEST_NOTE)
    return false;
  stack_parameters_t params{};
  params.spread = std::clamp(parameters_[0].GetInt(), 0, 255);
  params.wave = std::clamp(parameters_[1].GetInt(), 0, int(stackWaveLastItem));
  params.transpose = std::clamp(parameters_[2].GetInt(), -24, 24);
  params.attack = std::clamp(parameters_[5].GetInt(), 0, 255);
  params.decay = std::clamp(parameters_[6].GetInt(), 0, 255);
  params.sustain = std::clamp(parameters_[7].GetInt(), 0, 255);
  params.release = std::clamp(parameters_[8].GetInt(), 0, 255);
  params.volume = std::clamp(parameters_[9].GetInt(), 0, 255);
  params.brightness = std::clamp(parameters_[10].GetInt(), 0, 12);
  params.glide = std::clamp(parameters_[11].GetInt(), 0, 255);
  auto &voice = voices_[channel];
  voice.note_on(note, 255, retrigger, params);
  const unsigned chord = parameters_[12].GetInt();
  voice.set_chord((chord >> 12) & 15, (chord >> 8) & 15, (chord >> 4) & 15,
                  chord & 15);
  return true;
}

bool StackInstrument::Render(int channel, fixed *buffer, int size,
                             bool updateTick) {
  if (!buffer || size <= 0 || channel < 0 || channel >= SONG_CHANNEL_COUNT)
    return false;
  auto &voice = voices_[channel];
  if (voice.wave == stackWaveNone)
    return false;
  if (updateTick)
    voice.advance_arp();
  for (int i = 0; i < size; ++i)
    voice.sample(buffer + i * 2, buffer + i * 2 + 1);
  return true;
}

void StackInstrument::ProcessCommand(int channel, FourCC command,
                                     ushort value) {
  if (channel < 0 || channel >= SONG_CHANNEL_COUNT)
    return;
  auto &voice = voices_[channel];
  const uint8_t hi = value >> 8, lo = value & 0xFF;
  const auto signedNibble = [](int n) {
    return static_cast<int8_t>(n >= 8 ? n - 16 : n);
  };
  switch (command) {
  case FourCC::InstrumentCommandSetInstrumentParameter:
    voice.set_instrument_parameter(hi, lo);
    break;
  case FourCC::InstrumentCommandChordUp:
    voice.set_chord((value >> 12) & 15, (value >> 8) & 15, (value >> 4) & 15,
                    value & 15);
    break;
  case FourCC::InstrumentCommandChordDown:
    voice.set_chord(-int((value >> 12) & 15), -int((value >> 8) & 15),
                    -int((value >> 4) & 15), -int(value & 15));
    break;
  case FourCC::InstrumentCommandChordBidirectional:
    voice.set_chord(signedNibble((value >> 12) & 15),
                    signedNibble((value >> 8) & 15),
                    signedNibble((value >> 4) & 15), signedNibble(value & 15));
    break;
  case FourCC::InstrumentCommandArpeggiator:
    voice.command_arp(value);
    break;
  case FourCC::InstrumentCommandVibrato:
    voice.command_vibrato(hi, lo);
    break;
  case FourCC::InstrumentCommandPan:
    voice.command_pan(hi, lo);
    break;
  case FourCC::InstrumentCommandPitchSlide:
  case FourCC::InstrumentCommandLegato:
    voice.command_pitch(hi, static_cast<int8_t>(lo),
                        command == FourCC::InstrumentCommandLegato);
    break;
  case FourCC::InstrumentCommandPitchFineTune:
    voice.command_finetune(hi, static_cast<int8_t>(lo));
    break;
  case FourCC::InstrumentCommandKill:
    voice.stop();
    break;
  case FourCC::InstrumentCommandGateOff:
    voice.envelope.release_note();
    break;
  case FourCC::InstrumentCommandVolume:
    voice.volume = value & 0xFF;
    break;
  case FourCC::InstrumentCommandCrush:
    voice.bitcrush = value & 0x0F;
    break;
  default:
    break;
  }
}
