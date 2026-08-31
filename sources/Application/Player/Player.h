/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#ifndef _PLAYER_H_
#define _PLAYER_H_
#include "Application/Session/TrackerSessionState.h"
#include "Externals/etl/include/etl/string.h"
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "PlayerAudioReadiness.h"
#include "PlayerAudioActivity.h"
#include "PlayerMixer.h"
#include "PlayerTransportSnapshot.h"
#include "SyncMaster.h"
#include "System/Timer/Timer.h"
#include "TransportSnapshotPublication.h"
#include "config/StringLimits.h"

#include <atomic>

enum PlayerEventType { PET_START, PET_UPDATE, PET_STOP };

typedef uint32_t MixerStereoLevel;

class I_Instrument;

class PlayerEvent : public I_ObservableData {
public:
  PlayerEvent(PlayerEventType type, unsigned int tickCount = 0);
  PlayerEventType GetType();
  unsigned int GetTickCount();

private:
  PlayerEventType type_;
  unsigned int tickCount_;
};

class Player : public I_Observer,
               public Observable,
               public T_Singleton<Player> {
private: // Singleton
  friend class etl::singleton<Player>;
  Player();

public:
  bool Init(Project *, TrackerSessionState *);
  void BindProject(Project *, TrackerSessionState *);
  void Reset();
  void Close();

  virtual void Update(Observable &o, I_ObservableData *d);

  // basic interface

  void Start(PlayMode mode, bool forceSongMode, MixerServiceMode msmMode,
             bool stopAtEnd = false, int contextChannel = -1,
             int contextChainPosition = -1);
  void Stop();

  void SetSequencerMode(SequencerMode mode);
  SequencerMode GetSequencerMode();

  void OnStartButton(PlayMode origin, unsigned int from, bool startFromLastPos,
                     unsigned char chainPos,
                     MixerServiceMode msmMode = MSM_AUDIO,
                     bool stopAtEnd = false);
  void OnSongStartButton(unsigned int from, unsigned int to, bool requestStop,
                         bool forceImmediate,
                         MixerServiceMode msmMode = MSM_AUDIO,
                         bool stopAtEnd = false);
  bool IsPlaying();

  bool IsRunning();
  // True for every producer that can keep audio-owned project data alive,
  // including direct MIDI/sample-preview voices while transport is stopped.
  [[nodiscard]] bool IsAudioActive();
  bool GetStopAtEnd() { return stopAtEnd_; }

  void ProcessCommands();
  bool ProcessChannelCommand(int channel, FourCC cmd, ushort param);

  void StartStreaming(const char *name, int startSample = 0);
  void StartLoopingStreaming(const char *name);
  void StopStreaming();

  void StartRecordStreaming(uint16_t *srcBuffer, uint32_t size, bool stereo);
  void StopRecordStreaming();
  // Project/model lifetime boundary: stop transport, direct voices and both
  // auxiliary stream owners before their backing resources can be released.
  void StopAllAudio();

  // Channel data

  bool IsChannelPlaying(int channel);
  void SetChannelMute(int channel, bool mute);
  bool IsChannelMuted(int channel);

  // Live queuing

  QueueingMode GetQueueingMode(int i);
  unsigned char GetQueuePosition(int i);
  unsigned char GetQueueChainPosition(int i);
  void QueueChannel(int i, QueueingMode mode, unsigned char position,
                    unsigned char chainpos = 0);

  const char *GetLiveIndicator(int channel);
  double GetPlayTime();

  int GetPlayedNoteValue(int channel);
  bool GetPlayedSliceIndex(int channel, uint8_t &sliceIndex);

  // info
  int GetPlayedBufferPercentage();

  etl::array<stereosample, SONG_CHANNEL_COUNT> *GetMixerLevels();

  // master out, last avg level while playing
  stereosample GetMasterLevel();

  [[nodiscard]] PlayerTransportSnapshot CaptureTransportSnapshot() const;

  etl::string<STRING_AUDIO_API_MAX> GetAudioAPI();
  etl::string<STRING_AUDIO_DEVICE_MAX> GetAudioDevice();
  int GetAudioBufferSize();
  int GetAudioRequestedBufferSize();
  int GetAudioPreBufferCount();

  Project *GetProject() { return project_; }

  // Direct note playback methods for MIDI
  void PlayNote(unsigned short instrumentIndex, unsigned short channel,
                unsigned char note, unsigned char velocity);
  void StopNote(unsigned short instrumentIndex, unsigned short channel);

protected:
  void updateSongPos(int position, int channel, int chainPos = 0, int hop = -1);
  void updateChainPos(int position, int channel, int hop = 0);
  void updatePhrasePos(int pos, int channel);
  void playCursorPosition(int channel);
  void StepAutomationTableForRetrigger(int channel, I_Instrument *instrument);
  void RetriggerChannelInstrument(int channel, int semitoneOffset,
                                  bool stepAutomationTable);
  int getChannelHop(int channel, int pos);
  [[nodiscard]] bool moveToNextStep();
  void moveToNextPhrase(int channel, int hop = -1);
  void moveToNextChain(int channel, int hop);

  void triggerLiveChains();

  void SetAudioActive(bool active);
  void RefreshAudioActive();

  bool isPlayable(int row, int col, int chainPos = 0);
  bool findPlayable(uchar *row, int col, uchar chainPos = 0);

private:
  [[nodiscard]] PlayerTransportSnapshot BuildTransportSnapshotLocked() const;
  void PublishTransportSnapshotLocked();
  void QueueChannelLocked(int i, QueueingMode mode, unsigned char position,
                          unsigned char chainpos);

  PlayerMixer mixer_;
  TrackerSessionState *viewData_;
  Project *project_;

  SequencerMode sequencerMode_;
  PlayMode mode_;
  // Audio transport and UI presentation run on different cores on Node.
  // Publishing this edge atomically guarantees the UI observes the final
  // stopped frame and clears transport-dependent indicators such as VU meters.
  std::atomic<bool> isRunning_{false};
  PlayerAudioReadiness audioReadiness_;
  bool stopAtEnd_;

  unsigned long startClock_; // .Used to time display live queued chains
                             //  for blinking effect
  unsigned long now_;
  int lastPercentage_;
  unsigned int lastBeatCount_;
  unsigned char lastSongPos_;
  bool firstPlayCycle_;
  bool triggerLiveChains_;

  double startTime_;

  // Live queuing system

  unsigned char liveQueuePosition_[SONG_CHANNEL_COUNT];
  QueueingMode liveQueueingMode_[SONG_CHANNEL_COUNT];
  unsigned char liveQueueChainPosition_[SONG_CHANNEL_COUNT];
  unsigned int timeToLive_[SONG_CHANNEL_COUNT];
  unsigned int timeToStart_[SONG_CHANNEL_COUNT];

  // All producers are serialized by MixerService's existing mutex. UI and
  // diagnostics never take that audio-path lock; they validate an atomic-word
  // copy against the published frame sequence instead.
  TransportSnapshotPublication<PlayerTransportSnapshot> transportPublication_;

  bool retrigAllImmediate_;
  unsigned char retrigPos_;

  PlayerAudioActivity audioActivity_;
  bool audioActive_ = false;
};

#endif
