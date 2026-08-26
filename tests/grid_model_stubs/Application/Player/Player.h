/* Test-only player boundary used by the real UI2 tracker session adapter. */
#pragma once

#include "Application/Session/TrackerApplicationSession.h"

#include <array>

enum SequencerMode { SM_SONG, SM_LIVE };

class Player {
public:
  static Player *GetInstance() {
    static Player player;
    return &player;
  }

  void Reset() {
    sequencerMode_ = SM_SONG;
    muted_.fill(false);
    startCalls = 0;
    songStartCalls = 0;
    stopCalls = 0;
    running_ = false;
  }

  void SetSequencerMode(SequencerMode mode) { sequencerMode_ = mode; }
  SequencerMode GetSequencerMode() const { return sequencerMode_; }
  void OnStartButton(PlayMode origin, unsigned int from, bool startFromPrevious,
                     unsigned char chainPosition) {
    ++startCalls;
    lastOrigin = origin;
    lastFrom = from;
    lastStartFromPrevious = startFromPrevious;
    lastChainPosition = chainPosition;
    running_ = true;
  }
  void OnSongStartButton(unsigned int from, unsigned int to, bool requestStop,
                         bool forceImmediate) {
    ++songStartCalls;
    lastFrom = from;
    lastTo = to;
    lastRequestStop = requestStop;
    lastForceImmediate = forceImmediate;
  }
  void Stop() {
    ++stopCalls;
    running_ = false;
  }
  bool IsRunning() const { return running_; }
  void SetChannelMute(int channel, bool mute) { muted_[channel] = mute; }
  bool IsChannelMuted(int channel) const { return muted_[channel]; }

  int startCalls = 0;
  int songStartCalls = 0;
  int stopCalls = 0;
  PlayMode lastOrigin = PM_SONG;
  unsigned int lastFrom = 0U;
  unsigned int lastTo = 0U;
  unsigned char lastChainPosition = 0U;
  bool lastStartFromPrevious = false;
  bool lastRequestStop = false;
  bool lastForceImmediate = false;

private:
  SequencerMode sequencerMode_ = SM_SONG;
  std::array<bool, SONG_CHANNEL_COUNT> muted_{};
  bool running_ = false;
};
