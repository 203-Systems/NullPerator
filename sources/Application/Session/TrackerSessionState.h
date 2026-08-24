/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Model/Project.h"

enum PlayMode { PM_SONG, PM_CHAIN, PM_PHRASE, PM_LIVE, PM_AUDITION };

// Mutable editor/playback session state shared by the application controller
// and Player. It has no dependency on either UI implementation.
class TrackerSessionState {
public:
  static constexpr int VisibleSongRows = 16;

  explicit TrackerSessionState(Project *project);
  ~TrackerSessionState() = default;

  void Load(Project *project);

  unsigned char UpdateSongChain(int offset);
  void UpdateSongOffset(int offset);
  void UpdateSongCursor(int dx, int dy);
  void SetSongChain(unsigned char value);
  unsigned char *GetCurrentSongPointer();

  void UpdateChainCursor(int dx, int dy);
  unsigned char UpdateChainCursorValue(int offset, int dx, int dy);
  void SetChainPhrase(unsigned char value);
  unsigned char *GetCurrentChainPointer();

protected:
  void checkSongBoundaries();

  inline void updateData(unsigned char *value, int offset,
                         unsigned char limit, bool wrap) {
    int next = *value;
    if ((next == 0xFF) && (limit != 0xFF))
      next = 0;
    next += offset;
    if (next < 0)
      next = wrap ? limit + 1 + next : 0;
    if (next > limit)
      next = wrap ? next - (limit + 1) : limit;
    *value = static_cast<unsigned char>(next);
  }

public:
  Project *project_ = nullptr;
  Song *song_ = nullptr;

  int songX_ = 0;
  int songY_ = 0;
  int songOffset_ = 0;
  int chainRow_ = 0;
  int chainCol_ = 0;

  int currentChain_ = 0;
  int currentPhrase_ = 0;
  int currentInstrumentID_ = 0;
  int currentTable_ = 0;
  int currentGroove_ = 0;

  PlayMode playMode_ = PM_SONG;
  int songPlayPos_[SONG_CHANNEL_COUNT]{};
  unsigned char currentPlayChain_[SONG_CHANNEL_COUNT]{};
  int chainPlayPos_[SONG_CHANNEL_COUNT]{};
  unsigned char currentPlayPhrase_[SONG_CHANNEL_COUNT]{};
  int phrasePlayPos_[SONG_CHANNEL_COUNT]{};
  int phraseCurPos_ = 0;

  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> sampleEditorFilename;
  bool isShowingSampleEditorProjectPool = false;
  const char *importViewStartDir = nullptr;
};
