/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "TrackerSessionState.h"

TrackerSessionState::TrackerSessionState(Project *project) { Load(project); }

void TrackerSessionState::Load(Project *project) {
  project_ = project;
  song_ = &project->song_;
  currentChain_ = 0;
  currentPhrase_ = 0;
  songX_ = 0;
  songY_ = 0;
  songOffset_ = 0;
  chainCol_ = 0;
  chainRow_ = 0;
  currentTable_ = 0;
  currentInstrumentID_ = 0;
  currentGroove_ = 0;
  playMode_ = PM_SONG;
  phraseCurPos_ = 0;

  for (int i = 0; i < SONG_CHANNEL_COUNT; ++i) {
    songPlayPos_[i] = 0;
    chainPlayPos_[i] = 0;
    phrasePlayPos_[i] = 0;
    currentPlayChain_[i] = 0xFF;
    currentPlayPhrase_[i] = 0xFF;
  }

  sampleEditorFilename.clear();
  isShowingSampleEditorProjectPool = false;
  importViewStartDir = nullptr;
}

unsigned char TrackerSessionState::UpdateSongChain(int offset) {
  unsigned char *value =
      song_->data_ + songX_ + SONG_CHANNEL_COUNT * (songOffset_ + songY_);
  updateData(value, offset, CHAIN_COUNT - 1, false);
  return *value;
}

void TrackerSessionState::SetSongChain(unsigned char value) {
  unsigned char *cell =
      song_->data_ + songX_ + SONG_CHANNEL_COUNT * (songOffset_ + songY_);
  *cell = value;
}

void TrackerSessionState::UpdateSongOffset(int offset) {
  songOffset_ += offset;
  checkSongBoundaries();
}

void TrackerSessionState::UpdateSongCursor(int dx, int dy) {
  songX_ += dx;
  songY_ += dy;
  checkSongBoundaries();
}

void TrackerSessionState::checkSongBoundaries() {
  if (songX_ > SONG_CHANNEL_COUNT - 1)
    songX_ = SONG_CHANNEL_COUNT - 1;
  if (songX_ < 0)
    songX_ = 0;
  if (songY_ < 0) {
    songOffset_ += songY_;
    songY_ = 0;
  }
  if (songY_ > VisibleSongRows - 1) {
    songOffset_ += songY_ - VisibleSongRows + 1;
    songY_ = VisibleSongRows - 1;
  }
  if (songOffset_ > SONG_ROW_COUNT - VisibleSongRows)
    songOffset_ = SONG_ROW_COUNT - VisibleSongRows;
  if (songOffset_ < 0)
    songOffset_ = 0;
}

unsigned char *TrackerSessionState::GetCurrentSongPointer() {
  return song_->data_ + songX_ + SONG_CHANNEL_COUNT * (songOffset_ + songY_);
}

unsigned char TrackerSessionState::UpdateChainCursorValue(int offset, int dx,
                                                           int dy) {
  unsigned char *value = nullptr;
  unsigned char limit = 0;
  bool wrap = false;

  switch (chainCol_ + dx) {
  case 0:
    value = song_->chain_.data_ + (16 * currentChain_ + chainRow_ + dy);
    limit = PHRASE_COUNT - 1;
    break;
  case 1:
    value = song_->chain_.transpose_ + (16 * currentChain_ + chainRow_ + dy);
    limit = 0xFF;
    wrap = true;
    break;
  default:
    return 0;
  }
  updateData(value, offset, limit, wrap);
  return *value;
}

void TrackerSessionState::UpdateChainCursor(int dx, int dy) {
  chainCol_ += dx;
  chainRow_ += dy;
  if (chainCol_ > 1)
    chainCol_ = 1;
  if (chainCol_ < 0)
    chainCol_ = 0;
  if (chainRow_ > 15)
    chainRow_ = 15;
  if (chainRow_ < 0)
    chainRow_ = 0;
}

void TrackerSessionState::SetChainPhrase(unsigned char value) {
  unsigned char *cell = song_->chain_.data_ + (16 * currentChain_ + chainRow_);
  *cell = value;
}

unsigned char *TrackerSessionState::GetCurrentChainPointer() {
  return song_->chain_.data_ + (16 * currentChain_ + chainRow_);
}
