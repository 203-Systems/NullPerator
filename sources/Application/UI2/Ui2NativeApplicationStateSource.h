/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2GrooveController.h"
#include "Application/UI2/Controllers/Ui2DeviceController.h"
#include "Application/UI2/Controllers/Ui2ThemeController.h"
#include "Application/UI2/Controllers/Ui2ProjectController.h"
#include "Application/UI2/Controllers/Ui2TrackerControllerHub.h"
#include "Application/UI2/Ui2ApplicationStateSource.h"

class TrackerApplicationSession;

namespace ui2 {

// Native state projector. It reads the model plus UI2 controller values and
// writes one fixed-capacity renderer packet; no View, Field or GUIWindow is
// involved in this path.
class Ui2NativeApplicationStateSource final
    : public IUiApplicationStateSource {
public:
  Ui2NativeApplicationStateSource(TrackerApplicationSession &session,
                                  Ui2TrackerCommandExecutor &tracker,
                                  Ui2ProjectController &project,
                                  Ui2GrooveController &groove,
                                  Ui2DeviceController &device,
                                  Ui2ThemeController &theme)
      : session_(session), tracker_(tracker), project_(project),
        groove_(groove), device_(device), theme_(theme) {}

  void SetActivePage(UiApplicationPage page) { activePage_ = page; }

  [[nodiscard]] UiApplicationPage ActivePage() const override;
  [[nodiscard]] std::uint32_t NowMs() const override;
  [[nodiscard]] UiApplicationBatteryState ReadBattery() const override;

  [[nodiscard]] bool HasDialog() const override { return false; }
  [[nodiscard]] Ui2DialogSnapshot DialogSnapshot() const override {
    return {};
  }
  [[nodiscard]] std::uint32_t DialogInstanceId() const override { return 0; }

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
  CaptureFont(UiFontFrameState &state) override;
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
  TrackerApplicationSession &session_;
  Ui2TrackerCommandExecutor &tracker_;
  Ui2ProjectController &project_;
  Ui2GrooveController &groove_;
  Ui2DeviceController &device_;
  Ui2ThemeController &theme_;
  UiApplicationPage activePage_ = UiApplicationPage::Song;
};

} // namespace ui2
