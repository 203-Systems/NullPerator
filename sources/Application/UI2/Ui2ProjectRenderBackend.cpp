/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2ProjectRenderBackend.h"

#include "Application/Mixer/MixerService.h"
#include "Application/Model/Project.h"
#include "Application/Player/Player.h"
#include "Application/Session/TrackerSessionState.h"

namespace ui2 {

Ui2ProjectRenderStartResult
Ui2ProjectRenderBackend::Start(Ui2ProjectRenderMode mode) {
  Player *player = Player::GetInstance();
  if (player == nullptr)
    return Ui2ProjectRenderStartResult::BackendUnavailable;
  if (player->IsRunning())
    return Ui2ProjectRenderStartResult::PlayerBusy;
  if (!CanRenderFromFirstSongRow())
    return Ui2ProjectRenderStartResult::EmptyFirstSongRow;

  // This is intentionally the exact legacy ProjectView render start. In
  // particular forceSongMode and stopAtEnd are both true, so Player owns the
  // song traversal and closes every WavFileWriter through MixerService when
  // the last playable chain ends.
  player->Start(PM_SONG, true,
                mode == Ui2ProjectRenderMode::Stems ? MSM_FILESPLIT : MSM_FILE,
                true);
  if (!player->IsRunning())
    return Ui2ProjectRenderStartResult::BackendUnavailable;

  // AudioMixer historically logged a writer-open failure but Player continued
  // running, which made the UI claim a successful render with no output. UI2
  // observes the same writer state immediately and unwinds Player on failure;
  // no alternate renderer or platform-specific file path is introduced.
  if (!OutputReady(mode)) {
    player->Stop();
    return Ui2ProjectRenderStartResult::OutputUnavailable;
  }
  return Ui2ProjectRenderStartResult::Started;
}

void Ui2ProjectRenderBackend::Stop() {
  Player *player = Player::GetInstance();
  if (player != nullptr && player->IsRunning())
    player->Stop();
}

bool Ui2ProjectRenderBackend::IsRunning() const {
  Player *player = Player::GetInstance();
  return player != nullptr && player->IsRunning();
}

bool Ui2ProjectRenderBackend::Failed() const {
  const auto *mixer = MixerService::GetInstance();
  return mixer == nullptr || mixer->RenderFailed();
}

Ui2ProjectRenderPlaybackSnapshot
Ui2ProjectRenderBackend::CapturePlayback() const {
  Ui2ProjectRenderPlaybackSnapshot snapshot;
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    snapshot.songRow[channel] =
        static_cast<std::int16_t>(session_.songPlayPos_[channel]);
    snapshot.chainRow[channel] =
        static_cast<std::int8_t>(session_.chainPlayPos_[channel]);
    snapshot.phraseRow[channel] =
        static_cast<std::int8_t>(session_.phrasePlayPos_[channel]);
    snapshot.active[channel] =
        session_.currentPlayPhrase_[channel] != EMPTY_SONG_VALUE;
  }
  return snapshot;
}

int Ui2ProjectRenderBackend::ChainPhraseCount(int songRow, int channel) const {
  if (songRow < 0 || songRow >= SONG_ROW_COUNT || channel < 0 ||
      channel >= SONG_CHANNEL_COUNT)
    return 0;
  const unsigned char chain =
      project_.song_.data_[songRow * SONG_CHANNEL_COUNT + channel];
  if (chain == EMPTY_SONG_VALUE)
    return 0;
  int count = 0;
  for (int phrase = 0; phrase < PHRASES_PER_CHAIN; ++phrase) {
    if (project_.song_.chain_.data_[chain * PHRASES_PER_CHAIN + phrase] ==
        EMPTY_SONG_VALUE)
      break;
    ++count;
  }
  return count;
}

bool Ui2ProjectRenderBackend::CanRenderFromFirstSongRow() const {
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    const unsigned char chain = project_.song_.data_[channel];
    if (chain == EMPTY_SONG_VALUE)
      continue;
    // Legacy ProjectView accepts row 00 when any slot in one referenced chain
    // contains a phrase, even if malformed imported data has an earlier empty
    // slot. Keep that validation distinct from the contiguous phrase count
    // used by the progress algorithm.
    for (int phrase = 0; phrase < PHRASES_PER_CHAIN; ++phrase) {
      const unsigned char phraseId =
          project_.song_.chain_.data_[chain * PHRASES_PER_CHAIN + phrase];
      if (phraseId != EMPTY_SONG_VALUE)
        return true;
    }
  }
  return false;
}

bool Ui2ProjectRenderBackend::OutputReady(Ui2ProjectRenderMode mode) const {
  MixerService *mixer = MixerService::GetInstance();
  if (mixer == nullptr || mixer->RenderFailed())
    return false;
  if (mode == Ui2ProjectRenderMode::Mixdown) {
    AudioOut *output = mixer->GetAudioOut();
    return output != nullptr && output->IsRendering();
  }
  for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
    AudioMixer *bus = mixer->GetMixBus(channel);
    if (bus == nullptr || !bus->IsRendering())
      return false;
  }
  return true;
}

} // namespace ui2
