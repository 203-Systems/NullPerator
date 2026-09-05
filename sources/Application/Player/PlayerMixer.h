/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _APPLICATION_MIXER_H_
#define _APPLICATION_MIXER_H_

#include "Application/Audio/AudioFileStreamer.h"
#include "Application/Audio/RecordStreamer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Project.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "Foundation/Types/Fixed.h"
#include "PlayerChannel.h"
#include "PlayerMixerTelemetry.h"
#include "Services/Audio/AudioOut.h"

#include <atomic>
#include <cstdint>

#define STREAM_MIX_BUS 8

class PlayerMixer : public T_Singleton<PlayerMixer>,
                    public Observable,
                    public I_Observer {
public:
  PlayerMixer();
  virtual ~PlayerMixer() {};

  bool Start();
  void Stop();
  bool IsPlaying();
  bool Init(Project *project);
  void BindProject(Project *project);
  void Close();

  void OnPlayerStart(MixerServiceMode msmMode);
  void OnPlayerStop();

  void StartInstrument(int channel, I_Instrument *instrument,
                       unsigned char note, bool newInstrument);
  void StopInstrument(int channel);

  int GetChannelNote(int Channel);

  I_Instrument *GetInstrument(int channel);

  I_Instrument *GetLastInstrument(int channel);

  void StartChannel(int channel);
  void StopChannel(int channel);

  bool IsChannelPlaying(int channel) const;

  bool StartStreaming(const char *name, int startSample = 0);
  bool StartLoopingStreaming(const char *name);
  void StopStreaming();

  bool StartRecordStreaming(uint16_t *srcBuffer, uint32_t size, bool stereo);
  void StopRecordStreaming();

  stereosample GetMasterOutLevel();

  void Update(Observable &o, I_ObservableData *d);
  int GetPlayedBufferPercentage();

  void SetChannelMute(int channel, bool mute);
  bool IsChannelMuted(int channel);

  bool GetPlayedSliceIndex(int channel, uint8_t &sliceIndex);
  [[nodiscard]] PlayerMixerChannelTelemetry
  CaptureChannelTelemetry(int channel) const;

  AudioOut *GetAudioOut();

  void Lock();
  void Unlock();

  // Get the current project
  Project *GetProject() { return project_; }

  etl::array<stereosample, SONG_CHANNEL_COUNT> *GetMixerLevels();

private:
  Project *project_;
  etl::array<stereosample, SONG_CHANNEL_COUNT> mixerLevels_;

  I_Instrument *lastInstrument_[SONG_CHANNEL_COUNT];
  // Note, slice and channel-running state are sampled by UI2 on another core.
  // One packed word keeps the three values race-free and prevents a slice from
  // being paired with a note from a different trigger.
  std::atomic<std::uint32_t> channelTelemetry_[SONG_CHANNEL_COUNT]{};

  AudioFileStreamer fileStreamer_;
  RecordStreamer recordStreamer_;
  PlayerChannel *channel_[SONG_CHANNEL_COUNT];
};

#endif
