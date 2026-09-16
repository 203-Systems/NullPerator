/* SPDX-License-Identifier: BSD-3-Clause */
#include "GBInstrument.h"

namespace {
constexpr const char *kDuties[] = {"12.5%", "25%", "50%", "75%"};
constexpr const char *kLevels[] = {"MUTE", "100%", "50%", "25%"};
const Variable::Descriptor kCommon[] = {{FourCC::GBVolume, 128},
                                        {FourCC::GBTranspose, 0},
                                        {FourCC::GBTable, VAR_OFF},
                                        {FourCC::GBTableAuto, false},
                                        {FourCC::GBLength, 0}};
const Variable::Descriptor kPulse[] = {{FourCC::GBDuty, kDuties, 4, 2},
                                       {FourCC::GBEnvelope, 0xF0},
                                       {FourCC::GBSweep, 0}};
const Variable::Descriptor kNoise[] = {
    {FourCC::GBNoise, 0x35}, {FourCC::GBEnvelope, 0xF2}, {FourCC::Default, 0}};
const Variable::Descriptor kWave[] = {{FourCC::GBWaveLevel, kLevels, 4, 1},
                                      {FourCC::Default, 0},
                                      {FourCC::Default, 0}};
const Variable::Descriptor kWaveData[] = {
    {FourCC::GBWave0, 0x0123}, {FourCC::GBWave1, 0x4567},
    {FourCC::GBWave2, 0x89AB}, {FourCC::GBWave3, 0xCDEF},
    {FourCC::GBWave4, 0xFEDC}, {FourCC::GBWave5, 0xBA98},
    {FourCC::GBWave6, 0x7654}, {FourCC::GBWave7, 0x3210}};
const Variable::Descriptor *Specific(InstrumentType type) {
  return type == IT_GB_WAVE ? kWave : type == IT_GB_NOISE ? kNoise : kPulse;
}
} // namespace

GBInstrument::GBInstrument(InstrumentType type,
                           TrackVoicePool<gb::Voice> *voices)
    : I_Instrument(&variables_), type_(type),
      parameters_{Variable(kCommon[0]),        Variable(kCommon[1]),
                  Variable(kCommon[2]),        Variable(kCommon[3]),
                  Variable(kCommon[4]),        Variable(Specific(type)[0]),
                  Variable(Specific(type)[1]), Variable(Specific(type)[2])},
      voices_(voices) {
  tableState_.Reset();
  for (auto &parameter : parameters_)
    if (parameter.GetID() != FourCC::Default &&
        !(type == IT_GB_NOISE && parameter.GetID() == FourCC::GBTranspose))
      variables_.push_back(&parameter);
}
GBWaveInstrument::GBWaveInstrument(TrackVoicePool<gb::Voice> *voices)
    : GBInstrument(IT_GB_WAVE, voices),
      wave_{Variable(kWaveData[0]), Variable(kWaveData[1]),
            Variable(kWaveData[2]), Variable(kWaveData[3]),
            Variable(kWaveData[4]), Variable(kWaveData[5]),
            Variable(kWaveData[6]), Variable(kWaveData[7])} {
  for (auto &value : wave_)
    variables_.push_back(&value);
}
bool GBInstrument::Start(int channel, unsigned char note, bool retrigger) {
  if (!voices_.IsValid() || channel < 0 || channel >= SONG_CHANNEL_COUNT ||
      note > HIGHEST_NOTE)
    return false;
  auto &voice = voices_.Acquire(channel);
  voice.kind = type_ == IT_GB_WAVE    ? gb::Kind::Wave
               : type_ == IT_GB_NOISE ? gb::Kind::Noise
                                      : gb::Kind::Pulse;
  voice.volume = std::clamp(parameters_[0].GetInt(), 0, 255);
  voice.transpose = std::clamp(parameters_[1].GetInt(), -24, 24);
  voice.length =
      std::clamp(parameters_[4].GetInt(), 0, type_ == IT_GB_WAVE ? 256 : 64);
  const int mode = parameters_[5].GetInt();
  if (type_ == IT_GB_WAVE) {
    voice.waveLevel = std::clamp(mode, 0, 3);
    for (int i = 0; i < 8; ++i) {
      const auto word =
          FindVariable(static_cast<FourCC::enum_type>(FourCC::GBWave0 + i))
              ->GetInt();
      voice.wave[i * 2] = (word >> 8) & 255;
      voice.wave[i * 2 + 1] = word & 255;
    }
  } else {
    voice.envelope = parameters_[6].GetInt() & 255;
    if (type_ == IT_GB_PULSE) {
      voice.duty = std::clamp(mode, 0, 3);
      voice.sweep = parameters_[7].GetInt() & 127;
    } else
      voice.noise = mode & 255;
  }
  voice.trigger(note, retrigger);
  return true;
}
bool GBInstrument::Render(int channel, fixed *buffer, int size,
                          bool updateTick) {
  if (!buffer || size <= 0 || !voices_.Owns(channel) ||
      !voices_[channel].active)
    return false;
  auto &voice = voices_[channel];
  if (updateTick)
    voice.advanceArp();
  for (int i = 0; i < size; ++i)
    voice.sample(buffer + i * 2, buffer + i * 2 + 1);
  return true;
}
void GBInstrument::ProcessCommand(int channel, FourCC command, ushort value) {
  if (!voices_.Owns(channel))
    return;
  auto &voice = voices_[channel];
  const uint8_t hi = value >> 8, lo = value & 255;
  const int signedLo = lo >= 128 ? int(lo) - 256 : int(lo);
  const bool pitched = type_ != IT_GB_NOISE;
  switch (command) {
  case FourCC::InstrumentCommandKill:
  case FourCC::InstrumentCommandGateOff:
    voice.stop();
    break;
  case FourCC::InstrumentCommandVolume:
    voice.volume = lo;
    break;
  case FourCC::InstrumentCommandPan:
    voice.panTarget = lo;
    voice.panStep = lo > voice.pan ? int(hi) : -int(hi);
    if (!hi)
      voice.pan = lo;
    break;
  case FourCC::InstrumentCommandArpeggiator:
    if (pitched)
      voice.setArp(value);
    break;
  case FourCC::InstrumentCommandVibrato:
    if (pitched) {
      voice.vibratoRate = uint16_t(hi) << 6;
      voice.vibratoDepth = lo;
      voice.vibratoPhase = 0;
      voice.updatePitch();
    }
    break;
  case FourCC::InstrumentCommandPitchSlide:
  case FourCC::InstrumentCommandLegato:
    if (pitched) {
      if (command == FourCC::InstrumentCommandLegato && !lo &&
          voice.previousIncrement > 0) {
        voice.pitchFactor = static_cast<int32_t>(std::clamp<int64_t>(
            (int64_t(voice.previousIncrement) << 16) / voice.baseIncrement, 1,
            INT32_MAX));
        voice.slide(65536, hi);
      } else
        voice.slide(semitoneRatioQ16[128 + signedLo], hi);
    }
    break;
  case FourCC::InstrumentCommandPitchFineTune:
    if (pitched) {
      const int32_t delta =
          int32_t(semitoneRatioQ16[128 + (signedLo < 0 ? -1 : 1)]) - 65536;
      voice.slide(65536 + delta * std::abs(signedLo) / 128, hi);
    }
    break;
  case FourCC::InstrumentCommandSetInstrumentParameter:
    switch (hi) {
    case 0:
      if (type_ == IT_GB_WAVE)
        voice.waveLevel = lo & 3;
      else if (type_ == IT_GB_PULSE)
        voice.duty = lo & 3;
      else {
        voice.noise = lo;
        voice.timer = std::min(voice.timer, voice.timerPeriod());
      }
      break;
    case 1:
      if (pitched) {
        voice.transpose = std::clamp(signedLo, -24, 24);
        voice.baseIncrement = noteFrequency(int(voice.note) + voice.transpose);
        voice.updatePitch();
      }
      break;
    case 2:
      voice.volume = lo;
      break;
    case 3:
      voice.length = std::min<int>(lo, type_ == IT_GB_WAVE ? 256 : 64);
      break;
    case 4:
      if (type_ != IT_GB_WAVE)
        voice.setEnvelope(lo);
      break;
    case 5:
      if (type_ == IT_GB_PULSE) {
        voice.sweep = lo & 127;
        voice.sweepShadow = voice.period;
        voice.sweepTimer = ((lo >> 4) & 7) ? (lo >> 4) & 7 : 8;
      }
      break;
    default:
      // 10..2F address the 32 individual 4-bit wave-RAM samples.
      if (type_ == IT_GB_WAVE && hi >= 0x10 && hi <= 0x2F) {
        const int point = hi - 0x10, shift = (point & 1) ? 0 : 4;
        auto &byte = voice.wave[point / 2];
        byte = (byte & ~(15 << shift)) | ((lo & 15) << shift);
      }
      break;
    }
    break;
  default:
    break;
  }
}
