/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 nILS Podewski
 * Copyright (c) 2026 NullPerator contributors
 */
#include "DrumInstrument.h"
#include <algorithm>

namespace {
const Variable::Descriptor kDrumParameters[] = {
    {FourCC::DrumVoice0, defaultInstrument0},
    {FourCC::DrumVoice1, defaultInstrument1},
    {FourCC::DrumVoice2, defaultInstrument2},
    {FourCC::DrumVoice3, defaultInstrument3},
    {FourCC::DrumVoice4, defaultInstrument4},
    {FourCC::DrumVoice5, defaultInstrument5},
    {FourCC::DrumVoice6, defaultInstrument6},
    {FourCC::DrumVoice7, defaultInstrument7},
    {FourCC::DrumVoice8, defaultInstrument8},
    {FourCC::DrumVoice9, defaultInstrument9},
    {FourCC::DrumVoice10, defaultInstrument10},
    {FourCC::DrumVoice11, defaultInstrument11},
    {FourCC::DrumCharacter, 0},
};
} // namespace

DrumInstrument::DrumInstrument(TrackVoicePool<drum_voice_t> *voices)
    : I_Instrument(&variables_),
      parameters_{Variable(kDrumParameters[0]),  Variable(kDrumParameters[1]),
                  Variable(kDrumParameters[2]),  Variable(kDrumParameters[3]),
                  Variable(kDrumParameters[4]),  Variable(kDrumParameters[5]),
                  Variable(kDrumParameters[6]),  Variable(kDrumParameters[7]),
                  Variable(kDrumParameters[8]),  Variable(kDrumParameters[9]),
                  Variable(kDrumParameters[10]), Variable(kDrumParameters[11]),
                  Variable(kDrumParameters[12])},
      voices_(voices) {
  for (auto &parameter : parameters_)
    variables_.push_back(&parameter);
}

void DrumInstrument::OnStart() { voices_.Reset(); }

void DrumInstrument::Stop(int channel) {
  if (channel >= 0 && channel < SONG_CHANNEL_COUNT)
    voices_.Stop(channel);
}

bool DrumInstrument::Start(int channel, unsigned char note, bool retrigger) {
  if (!voices_.IsValid() || channel < 0 || channel >= SONG_CHANNEL_COUNT ||
      note > HIGHEST_NOTE)
    return false;
  const unsigned packed = parameters_[note % 12].GetInt();
  drum_parameters_t params{};
  params.wave = packed & 0xF;
  params.decay = (packed >> 4) & 0xF;
  params.note = (packed >> 8) & 0xF;
  params.pitch = (packed >> 12) & 0xF;
  params.character = std::clamp(parameters_[12].GetInt(), 0, 255);
  voices_.Acquire(channel).note_on(note, 255, retrigger, params);
  return true;
}

bool DrumInstrument::Render(int channel, fixed *buffer, int size, bool) {
  if (!buffer || size <= 0 || !voices_.Owns(channel))
    return false;
  auto &voice = voices_[channel];
  if (voice.wave == drumWaveNone)
    return false;
  for (int i = 0; i < size; ++i)
    voice.sample(buffer + i * 2, buffer + i * 2 + 1);
  return true;
}

void DrumInstrument::ProcessCommand(int channel, FourCC command, ushort value) {
  if (!voices_.Owns(channel))
    return;
  auto &voice = voices_[channel];
  switch (command) {
  case FourCC::InstrumentCommandKill:
    voice.stop();
    break;
  case FourCC::InstrumentCommandGateOff:
    voice.stop();
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
