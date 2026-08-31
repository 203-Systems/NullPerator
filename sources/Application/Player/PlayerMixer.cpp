/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "PlayerMixer.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/fixed.h"
#include "Services/Midi/MidiService.h"
#include "SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <cstdint>
#include <math.h>
#include <stdlib.h>

namespace {

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "audio telemetry requires lock-free 32-bit atomics");

std::int8_t PlayedSliceFor(std::uint8_t note, I_Instrument *instrument) {
  if (instrument == nullptr || instrument->GetType() != IT_SAMPLE ||
      note == NO_NOTE) {
    return -1;
  }
  auto *sampleInstrument = static_cast<SampleInstrument *>(instrument);
  if (!sampleInstrument->ShouldDisplaySliceForNote(note))
    return -1;
  return static_cast<std::int8_t>(note - SampleInstrument::SliceNoteBase);
}

} // namespace

PlayerMixer::PlayerMixer() {

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastInstrument_[i] = 0;
    channelTelemetry_[i].store(PlayerMixerTelemetry::Pack(NO_NOTE, -1, false),
                               std::memory_order_relaxed);
  };

  alignas(PlayerChannel) static char
      playerChannelMemBuf[sizeof(PlayerChannel) * SONG_CHANNEL_COUNT];
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    channel_[i] =
        new (playerChannelMemBuf + i * sizeof(PlayerChannel)) PlayerChannel(i);
  }
}

bool PlayerMixer::Init(Project *project) {

  MixerService *ms = MixerService::GetInstance();
  if (!ms->Init()) {
    return false;
  }

  AudioMixer *audioMixer = ms->GetMixBus(STREAM_MIX_BUS);
  audioMixer->AddModule(fileStreamer_);

  project_ = project;

  // Add the record mixer
  audioMixer->AddModule(recordStreamer_);

  // Init states
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastInstrument_[i] = 0;
  };

  // Setup mixbus
  Mixer *mixer = Mixer::GetInstance();
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    channel_[i]->SetMixBus(mixer->GetBus(i));
  }

  // streamer need access to project to get current volume
  fileStreamer_.SetProject(project);

  return true;
};

void PlayerMixer::BindProject(Project *project) {
  project_ = project;
  fileStreamer_.SetProject(project);

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    lastInstrument_[i] = 0;
    channelTelemetry_[i].store(PlayerMixerTelemetry::Pack(NO_NOTE, -1, false),
                               std::memory_order_relaxed);
  }
}

void PlayerMixer::Close() {

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    channel_[i]->Reset();
  }

  MixerService *ms = MixerService::GetInstance();
  ms->Close();
}

bool PlayerMixer::Start() {
  MixerService *ms = MixerService::GetInstance();
  ms->AddObserver(*this);

  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
    const PlayerMixerChannelTelemetry current = CaptureChannelTelemetry(i);
    channelTelemetry_[i].store(
        PlayerMixerTelemetry::Pack(NO_NOTE, -1, current.playing),
        std::memory_order_relaxed);
  };

  if (!ms->Start()) {
    ms->RemoveObserver(*this);
    return false;
  }
  return true;
};

void PlayerMixer::Stop() {
  MixerService *ms = MixerService::GetInstance();
  ms->Stop();
  ms->RemoveObserver(*this);
};

void PlayerMixer::StartChannel(int channel) {
  channelTelemetry_[channel].fetch_or(PlayerMixerTelemetry::PlayingBit,
                                      std::memory_order_relaxed);
};

void PlayerMixer::StopChannel(int channel) {

  StopInstrument(channel);
  channelTelemetry_[channel].fetch_and(~PlayerMixerTelemetry::PlayingBit,
                                       std::memory_order_relaxed);
};

bool PlayerMixer::IsChannelPlaying(int channel) const {
  return CaptureChannelTelemetry(channel).playing;
};

I_Instrument *PlayerMixer::GetLastInstrument(int channel) {
  return lastInstrument_[channel];
};

stereosample PlayerMixer::GetMasterOutLevel() {
  MixerService *ms = MixerService::GetInstance();
  return ms->GetMasterBus()->GetMixerLevels();
}

etl::array<stereosample, SONG_CHANNEL_COUNT> *PlayerMixer::GetMixerLevels() {
  MixerService *ms = MixerService::GetInstance();

  // Get the current mixer levels from each bus
  for (int i = 0; i < 8; i++) {
    AudioMixer *audioMixer = ms->GetMixBus(i);
    mixerLevels_[i] = audioMixer->GetMixerLevels();
  }

  return &mixerLevels_;
}

void PlayerMixer::Update(Observable &o, I_ObservableData *d) {

  // Notifies the player so that pattern data is processed

  SetChanged();
  NotifyObservers();

  MixerService *ms = MixerService::GetInstance();
  ms->SetMasterVolume(project_->GetMasterVolume());
};

void PlayerMixer::StartInstrument(int channel, I_Instrument *instrument,
                                  unsigned char note, bool newInstrument) {
  channel_[channel]->StartInstrument(instrument, note, newInstrument);
  lastInstrument_[channel] = instrument;
  const bool playing = CaptureChannelTelemetry(channel).playing;
  channelTelemetry_[channel].store(
      PlayerMixerTelemetry::Pack(note, PlayedSliceFor(note, instrument),
                                 playing),
      std::memory_order_relaxed);
};

void PlayerMixer::StopInstrument(int channel) {
  channel_[channel]->StopInstrument();
  const bool playing = CaptureChannelTelemetry(channel).playing;
  channelTelemetry_[channel].store(
      PlayerMixerTelemetry::Pack(NO_NOTE, -1, playing),
      std::memory_order_relaxed);
}

I_Instrument *PlayerMixer::GetInstrument(int channel) {
  return channel_[channel]->GetInstrument();
}

int PlayerMixer::GetPlayedBufferPercentage() {
  MixerService *ms = MixerService::GetInstance();
  return ms->GetPlayedBufferPercentage();
};

void PlayerMixer::SetChannelMute(int channel, bool mode) {
  channel_[channel]->SetMute(mode);
}

bool PlayerMixer::IsChannelMuted(int channel) {
  return channel_[channel]->IsMuted();
}

bool PlayerMixer::StartStreaming(const char *name, int startSample) {
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
  const bool started = fileStreamer_.Start(name, startSample);
  ms->Unlock();
  return started;
};

bool PlayerMixer::StartLoopingStreaming(const char *name) {
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
  const bool started = fileStreamer_.Start(name, 0, true);
  ms->Unlock();
  return started;
};

void PlayerMixer::StopStreaming() {
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
  fileStreamer_.Stop();
  ms->Unlock();
};

bool PlayerMixer::StartRecordStreaming(uint16_t *srcBuffer, uint32_t size,
                                       bool stereo) {
  return recordStreamer_.Start(srcBuffer, size, stereo);
};

void PlayerMixer::StopRecordStreaming() { recordStreamer_.Stop(); };

bool PlayerMixer::IsPlaying() { return fileStreamer_.IsPlaying(); }

void PlayerMixer::OnPlayerStart(MixerServiceMode msmMode) {
  MixerService *ms = MixerService::GetInstance();
  ms->OnPlayerStart(msmMode);
}

void PlayerMixer::OnPlayerStop() {
  MixerService *ms = MixerService::GetInstance();
  ms->OnPlayerStop();
}

PlayerMixerChannelTelemetry
PlayerMixer::CaptureChannelTelemetry(int channel) const {
  return PlayerMixerTelemetry::Decode(
      channelTelemetry_[channel].load(std::memory_order_relaxed));
}

int PlayerMixer::GetChannelNote(int channel) {
  return CaptureChannelTelemetry(channel).note;
}

bool PlayerMixer::GetPlayedSliceIndex(int channel, uint8_t &sliceIndex) {
  const PlayerMixerChannelTelemetry telemetry =
      CaptureChannelTelemetry(channel);
  if (telemetry.slice < 0)
    return false;
  sliceIndex = static_cast<std::uint8_t>(telemetry.slice);
  return true;
}

AudioOut *PlayerMixer::GetAudioOut() {
  MixerService *ms = MixerService::GetInstance();
  return ms->GetAudioOut();
};

void PlayerMixer::Lock() {
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
};

void PlayerMixer::Unlock() {
  MixerService *ms = MixerService::GetInstance();
  ms->Unlock();
};
