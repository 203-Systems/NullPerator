/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/TrackerInput.h"
#include "Application/Model/Song.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

namespace ui2 {

enum class Ui2ProjectRenderMode : std::uint8_t { Mixdown, Stems };

enum class Ui2ProjectRenderStartResult : std::uint8_t {
  Started,
  PlayerBusy,
  EmptyFirstSongRow,
  BackendUnavailable,
  OutputUnavailable,
};

struct Ui2ProjectRenderPlaybackSnapshot {
  std::array<std::int16_t, SONG_CHANNEL_COUNT> songRow{};
  std::array<std::int8_t, SONG_CHANNEL_COUNT> chainRow{};
  std::array<std::int8_t, SONG_CHANNEL_COUNT> phraseRow{};
  std::array<bool, SONG_CHANNEL_COUNT> active{};
};

// The real implementation is a thin adapter over Project, TrackerSessionState,
// Player and MixerService. Keeping this boundary read-only except for
// Start/Stop makes the fixed-capacity lifecycle independently testable without
// constructing the audio graph.
class IUi2ProjectRenderBackend {
public:
  virtual ~IUi2ProjectRenderBackend() = default;

  [[nodiscard]] virtual Ui2ProjectRenderStartResult
  Start(Ui2ProjectRenderMode mode) = 0;
  virtual void Stop() = 0;
  [[nodiscard]] virtual bool IsRunning() const = 0;
  [[nodiscard]] virtual Ui2ProjectRenderPlaybackSnapshot
  CapturePlayback() const = 0;
  [[nodiscard]] virtual int ChainPhraseCount(int songRow,
                                             int channel) const = 0;
};

// Owns the complete UI2 render lifecycle: start validation, the approved
// two-line progress dialog, legacy song-percent tracking, cancellation and
// completion acknowledgement. It performs no allocation and is polled from
// the application tick; no work is performed by the frame renderer.
class Ui2ProjectRenderController final {
public:
  explicit Ui2ProjectRenderController(IUi2ProjectRenderBackend &backend)
      : backend_(&backend) {}

  [[nodiscard]] bool Active() const { return phase_ != Phase::Idle; }
  [[nodiscard]] bool Rendering() const { return phase_ == Phase::Rendering; }
  [[nodiscard]] std::uint32_t InstanceId() const { return instanceId_; }
  [[nodiscard]] Ui2ProjectRenderMode Mode() const { return mode_; }
  [[nodiscard]] Ui2ProjectRenderStartResult LastStartResult() const {
    return lastStartResult_;
  }
  [[nodiscard]] int ProgressPercent() const {
    return phase_ == Phase::Complete ? 100 : CalculatePercent();
  }

  void Reset() {
    if (phase_ == Phase::Rendering && backend_ != nullptr &&
        backend_->IsRunning())
      backend_->Stop();
    phase_ = Phase::Idle;
    message_ = Message::None;
    input_ = {};
    ResetProgress();
    ++instanceId_;
  }

  bool Request(Ui2ProjectRenderMode mode) {
    if (Active() || backend_ == nullptr) {
      lastStartResult_ = Ui2ProjectRenderStartResult::BackendUnavailable;
      return false;
    }

    const Ui2ProjectRenderStartResult result = backend_->Start(mode);
    lastStartResult_ = result;
    switch (result) {
    case Ui2ProjectRenderStartResult::Started:
      mode_ = mode;
      phase_ = Phase::Rendering;
      message_ = Message::None;
      ResetProgress();
      input_ = {};
      ++instanceId_;
      UpdateProgress(backend_->CapturePlayback());
      return true;
    case Ui2ProjectRenderStartResult::PlayerBusy:
      // ProjectView silently ignored Render while Player was already active.
      // Preserve that transport behavior instead of stacking an unrelated
      // modal over playback.
      return false;
    case Ui2ProjectRenderStartResult::EmptyFirstSongRow:
      ShowMessage(Message::EmptyFirstSongRow);
      return false;
    case Ui2ProjectRenderStartResult::BackendUnavailable:
    case Ui2ProjectRenderStartResult::OutputUnavailable:
      ShowMessage(Message::OutputUnavailable);
      return false;
    }
    return false;
  }

  void Tick() {
    if (phase_ != Phase::Rendering || backend_ == nullptr)
      return;
    if (backend_->IsRunning()) {
      UpdateProgress(backend_->CapturePlayback());
      return;
    }
    phase_ = Phase::Complete;
    renderedUnits_ = totalRenderUnits_;
  }

  void Handle(TrackerAction action, bool pressed) {
    if (!Active() || !input_.Update(action, pressed) || !pressed)
      return;
    if (action != TrackerAction::Enter)
      return;

    // Resolve a natural stop before interpreting ENTER. This prevents a final
    // key press racing the audio callback from being reported as a cancel.
    Tick();
    if (phase_ == Phase::Rendering) {
      if (backend_ != nullptr && backend_->IsRunning())
        backend_->Stop();
      ShowMessage(Message::RenderingStopped);
      return;
    }
    Dismiss();
  }

  [[nodiscard]] Ui2DialogSnapshot Snapshot() const {
    Ui2DialogSnapshot snapshot;
    if (phase_ == Phase::Message) {
      snapshot.kind = UiDialogKind::Message;
      switch (message_) {
      case Message::EmptyFirstSongRow:
        snapshot.SetTitle("Render failed");
        snapshot.SetLabel("Song row 00 has no phrases");
        break;
      case Message::OutputUnavailable:
        snapshot.SetTitle("Render failed");
        snapshot.SetLabel("Could not open file");
        break;
      case Message::RenderingStopped:
        snapshot.SetTitle("Rendering Stopped");
        break;
      case Message::None:
        break;
      }
      snapshot.PushAction(UiDialogAction::Ok);
      snapshot.SetSelectedAction(0, true);
      return snapshot;
    }

    snapshot.kind = UiDialogKind::RenderProgress;
    snapshot.SetTitle(mode_ == Ui2ProjectRenderMode::Stems
                          ? "Stems Rendering"
                          : "Rendering");
    if (phase_ == Phase::Complete)
      snapshot.SetLabel("Render Complete!");
    char percent[Ui2DialogSnapshot::ElapsedCapacity]{};
    FormatPercent(ProgressPercent(), percent);
    snapshot.SetElapsed(percent);
    snapshot.SetProgressPercent(ProgressPercent());
    snapshot.PushAction(phase_ == Phase::Complete ? UiDialogAction::Ok
                                                   : UiDialogAction::Cancel);
    snapshot.SetSelectedAction(0, true);
    return snapshot;
  }

private:
  enum class Phase : std::uint8_t { Idle, Rendering, Complete, Message };
  enum class Message : std::uint8_t {
    None,
    EmptyFirstSongRow,
    OutputUnavailable,
    RenderingStopped,
  };

  void ResetProgress() {
    startSongRow_ = 0;
    renderedUnits_ = 0;
    totalRenderUnits_ = 1;
    progressChannel_ = -1;
    startSongRowCaptured_ = false;
  }

  static void FormatPercent(
      int value, char (&output)[Ui2DialogSnapshot::ElapsedCapacity]) {
    const int percent = std::clamp(value, 0, 100);
    int cursor = 0;
    if (percent >= 100) {
      output[cursor++] = '1';
      output[cursor++] = '0';
      output[cursor++] = '0';
    } else {
      if (percent >= 10)
        output[cursor++] = static_cast<char>('0' + percent / 10);
      output[cursor++] = static_cast<char>('0' + percent % 10);
    }
    output[cursor] = '%';
  }

  void ShowMessage(Message message) {
    phase_ = Phase::Message;
    message_ = message;
    input_ = {};
    ++instanceId_;
  }

  void Dismiss() {
    phase_ = Phase::Idle;
    message_ = Message::None;
    input_ = {};
    ++instanceId_;
  }

  void UpdateProgress(const Ui2ProjectRenderPlaybackSnapshot &playback) {
    bool hasActive = false;
    int currentRow = 0;
    for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
      if (!playback.active[channel])
        continue;
      const int row = std::clamp<int>(playback.songRow[channel], 0,
                                      SONG_ROW_COUNT - 1);
      if (!hasActive || row > currentRow) {
        currentRow = row;
        hasActive = true;
      }
    }
    if (!hasActive)
      return;

    if (!startSongRowCaptured_) {
      startSongRow_ = currentRow;
      startSongRowCaptured_ = true;
      InitializeProgressChannel();
    }
    if (progressChannel_ < 0)
      return;
    renderedUnits_ = std::max(
        renderedUnits_, CalculateRenderedUnits(progressChannel_, playback));
  }

  void InitializeProgressChannel() {
    progressChannel_ = -1;
    totalRenderUnits_ = 1;
    int bestTotalUnits = 0;
    for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
      const int total = CalculateTotalUnits(channel);
      if (total <= 0)
        continue;
      if (progressChannel_ < 0 || total < bestTotalUnits) {
        progressChannel_ = channel;
        bestTotalUnits = total;
      }
    }
    if (progressChannel_ >= 0 && bestTotalUnits > 0)
      totalRenderUnits_ = bestTotalUnits;
  }

  [[nodiscard]] int CalculateTotalUnits(int channel) const {
    if (backend_ == nullptr || startSongRow_ < 0 ||
        startSongRow_ >= SONG_ROW_COUNT)
      return 0;
    int total = 0;
    for (int row = startSongRow_; row < SONG_ROW_COUNT; ++row) {
      const int phraseCount = backend_->ChainPhraseCount(row, channel);
      if (phraseCount <= 0)
        break;
      total += phraseCount * STEPS_PER_PHRASE;
      if (row + 1 >= SONG_ROW_COUNT ||
          backend_->ChainPhraseCount(row + 1, channel) <= 0)
        break;
    }
    return total;
  }

  [[nodiscard]] int CalculateRenderedUnits(
      int channel, const Ui2ProjectRenderPlaybackSnapshot &playback) const {
    if (backend_ == nullptr || channel < 0 ||
        channel >= SONG_CHANNEL_COUNT)
      return 0;
    const int currentSongRow = playback.songRow[channel];
    if (currentSongRow < startSongRow_)
      return 0;

    int rendered = 0;
    for (int row = startSongRow_;
         row < currentSongRow && row < SONG_ROW_COUNT; ++row) {
      const int phraseCount = backend_->ChainPhraseCount(row, channel);
      if (phraseCount <= 0)
        return rendered;
      rendered += phraseCount * STEPS_PER_PHRASE;
    }
    if (currentSongRow >= SONG_ROW_COUNT)
      return totalRenderUnits_;

    const int phraseCount =
        backend_->ChainPhraseCount(currentSongRow, channel);
    if (phraseCount <= 0)
      return rendered;
    const int chainRow =
        std::clamp<int>(playback.chainRow[channel], 0, phraseCount - 1);
    const int phraseRow = std::clamp<int>(playback.phraseRow[channel], 0,
                                          STEPS_PER_PHRASE - 1);
    rendered += chainRow * STEPS_PER_PHRASE + phraseRow;
    return std::min(rendered, totalRenderUnits_);
  }

  [[nodiscard]] int CalculatePercent() const {
    if (!startSongRowCaptured_ || progressChannel_ < 0)
      return 0;
    const int total = std::max(totalRenderUnits_, 1);
    const int rendered = std::clamp(renderedUnits_, 0, total);
    return std::min((rendered * 100) / total, 99);
  }

  IUi2ProjectRenderBackend *backend_ = nullptr;
  Ui2ControllerInputState input_{};
  std::uint32_t instanceId_ = 0U;
  int startSongRow_ = 0;
  int renderedUnits_ = 0;
  int totalRenderUnits_ = 1;
  int progressChannel_ = -1;
  Ui2ProjectRenderMode mode_ = Ui2ProjectRenderMode::Mixdown;
  Ui2ProjectRenderStartResult lastStartResult_ =
      Ui2ProjectRenderStartResult::BackendUnavailable;
  Phase phase_ = Phase::Idle;
  Message message_ = Message::None;
  bool startSongRowCaptured_ = false;
};

static_assert(std::is_trivially_copyable_v<Ui2ProjectRenderPlaybackSnapshot>);
static_assert(sizeof(Ui2ProjectRenderController) <= 64U,
              "render lifecycle must remain embedded-friendly");

} // namespace ui2
