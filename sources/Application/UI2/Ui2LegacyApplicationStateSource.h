/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Ui2ApplicationStateSource.h"

class AppWindow;

namespace ui2 {

// Compatibility bridge used while AppWindow and the legacy View controllers
// remain the production owners of navigation and model mutation. Keeping this
// adapter outside UiApplicationRuntime makes that dependency replaceable
// without changing the renderer or its platform presenters.
class UiLegacyApplicationStateSource final : public IUiApplicationStateSource {
public:
  explicit UiLegacyApplicationStateSource(AppWindow &window)
      : window_(window) {}

  [[nodiscard]] UiApplicationPage ActivePage() const override;
  [[nodiscard]] std::uint32_t NowMs() const override;
  [[nodiscard]] UiApplicationBatteryState ReadBattery() const override;

  [[nodiscard]] bool HasDialog() const override;
  [[nodiscard]] Ui2DialogSnapshot DialogSnapshot() const override;
  [[nodiscard]] std::uint32_t DialogInstanceId() const override;

  [[nodiscard]] UiApplicationActivityState
  CaptureSong(UiSongFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureChain(UiChainFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CapturePhrase(UiPhraseFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureTable(UiTableFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureInstrument(UiInstrumentFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureProject(UiProjectFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureDevice(UiDeviceFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureTheme(UiThemeFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureBrowser(UiBrowserFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureGroove(UiGrooveFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureMixer(UiMixerFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureSampleEditor(UiSampleEditorFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureSampleSlices(UiSampleSlicesFrameState &state) override;
  [[nodiscard]] UiApplicationActivityState
  CaptureRecord(UiRecordFrameState &state) override;

private:
  AppWindow &window_;
};

} // namespace ui2
