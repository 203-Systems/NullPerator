/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2ProjectRenderController.h"

class Project;
class TrackerSessionState;

namespace ui2 {

// Production bridge for Project render actions. Audio generation and WAV
// ownership deliberately remain in the established Player -> PlayerMixer ->
// MixerService -> AudioMixer/WavFileWriter path; UI2 only controls and observes
// that path.
class Ui2ProjectRenderBackend final : public IUi2ProjectRenderBackend {
public:
  Ui2ProjectRenderBackend(Project &project, TrackerSessionState &session)
      : project_(project), session_(session) {}

  [[nodiscard]] Ui2ProjectRenderStartResult
  Start(Ui2ProjectRenderMode mode) override;
  void Stop() override;
  [[nodiscard]] bool IsRunning() const override;
  [[nodiscard]] bool Failed() const override;
  [[nodiscard]] Ui2ProjectRenderPlaybackSnapshot
  CapturePlayback() const override;
  [[nodiscard]] int ChainPhraseCount(int songRow, int channel) const override;

private:
  [[nodiscard]] bool CanRenderFromFirstSongRow() const;
  [[nodiscard]] bool OutputReady(Ui2ProjectRenderMode mode) const;

  Project &project_;
  TrackerSessionState &session_;
};

} // namespace ui2
