/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/UI2/Controllers/Ui2DeviceController.h"
#include "Application/UI2/Controllers/Ui2DeviceLifecycleController.h"
#include "Application/UI2/Controllers/Ui2ClipboardNoticeController.h"
#include "Application/UI2/Controllers/Ui2FeedbackController.h"
#include "Application/UI2/Controllers/Ui2FontController.h"
#include "Application/UI2/Controllers/Ui2GrooveController.h"
#include "Application/UI2/Controllers/Ui2InstrumentController.h"
#include "Application/UI2/Controllers/Ui2InstrumentBrowserController.h"
#include "Application/UI2/Controllers/Ui2InstrumentLifecycleController.h"
#include "Application/UI2/Controllers/Ui2MixerController.h"
#include "Application/UI2/Controllers/Ui2ProjectBrowserController.h"
#include "Application/UI2/Controllers/Ui2ProjectController.h"
#include "Application/UI2/Controllers/Ui2ProjectLifecycleController.h"
#include "Application/UI2/Controllers/Ui2ProjectRenderController.h"
#include "Application/UI2/Controllers/Ui2RecordController.h"
#include "Application/UI2/Controllers/Ui2RenameController.h"
#include "Application/UI2/Controllers/Ui2SampleBrowserController.h"
#include "Application/UI2/Controllers/Ui2SampleEditorController.h"
#include "Application/UI2/Controllers/Ui2SampleSlicesController.h"
#include "Application/UI2/Controllers/Ui2SettingsBrowserController.h"
#include "Application/UI2/Controllers/Ui2ThemeController.h"
#include "Application/UI2/Controllers/Ui2TrackerControllerHub.h"
#include "Application/UI2/Ui2ApplicationStateSource.h"
#include "Application/UI2/Ui2PersistenceStatus.h"
#include "Application/UI2/Workflows/Ui2GrooveWorkflow.h"

class TrackerApplicationSession;
class FirmwareLifecycleService;

namespace ui2 {

// Native state projector. It reads the model plus UI2 controller values and
// writes one fixed-capacity renderer packet; no View, Field or GUIWindow is
// involved in this path.
class Ui2NativeApplicationStateSource final : public IUiApplicationStateSource {
public:
  Ui2NativeApplicationStateSource(
      TrackerApplicationSession &session, Ui2TrackerCommandExecutor &tracker,
      Ui2ProjectController &project,
      Ui2ProjectBrowserController &projectBrowser,
      Ui2SettingsBrowserController &settingsBrowser,
      Ui2ClipboardNoticeController &clipboardNotice,
      Ui2FeedbackController &feedback,
      Ui2ProjectLifecycleController &projectLifecycle,
      Ui2ProjectRenderController &projectRender, Ui2GrooveController &groove,
      Ui2GrooveClipboard &grooveClipboard,
      Ui2DeviceController &device,
      Ui2DeviceLifecycleController &deviceLifecycle,
      Ui2ThemeController &theme, Ui2FontController &font,
      Ui2RenameController &rename, Ui2MixerController &mixer,
      Ui2InstrumentController &instrument,
      Ui2InstrumentLifecycleController &instrumentLifecycle,
      Ui2InstrumentBrowserController &instrumentBrowser,
      Ui2SampleBrowserController &sampleBrowser,
      Ui2SampleEditorController &sampleEditor,
      Ui2SampleSlicesController &sampleSlices,
      Ui2RecordController &record,
      FirmwareLifecycleService &firmwareLifecycle,
      const Ui2PersistenceStatus &persistenceStatus)
      : session_(session), tracker_(tracker), project_(project),
        projectBrowser_(projectBrowser), settingsBrowser_(settingsBrowser),
        clipboardNotice_(clipboardNotice), feedback_(feedback),
        projectLifecycle_(projectLifecycle), projectRender_(projectRender),
        groove_(groove), grooveClipboard_(grooveClipboard), device_(device),
        deviceLifecycle_(deviceLifecycle),
        theme_(theme), font_(font), rename_(rename), mixer_(mixer),
        instrument_(instrument), instrumentLifecycle_(instrumentLifecycle),
        instrumentBrowser_(instrumentBrowser), sampleBrowser_(sampleBrowser),
        sampleEditor_(sampleEditor), sampleSlices_(sampleSlices),
        record_(record), firmwareLifecycle_(firmwareLifecycle),
        persistenceStatus_(persistenceStatus) {}

  void SetActivePage(UiApplicationPage page) { activePage_ = page; }
  void SetNavigationHeld(bool held) { navigationHeld_ = held; }
  void SetInstrumentBrowserActive(bool active) {
    instrumentBrowserActive_ = active;
  }

  [[nodiscard]] UiApplicationPage ActivePage() const override;
  [[nodiscard]] std::uint32_t NowMs() const override;
  [[nodiscard]] UiApplicationBatteryState ReadBattery() const override;
  [[nodiscard]] bool PersistenceSaving() const override {
    return persistenceStatus_.Saving();
  }
  [[nodiscard]] bool NavigationHeld() const override { return navigationHeld_; }
  [[nodiscard]] UiTextCaseMode TextCase() const override;

  [[nodiscard]] bool HasDialog() const override {
    return projectRender_.Active() || projectLifecycle_.Active() ||
           sampleBrowser_.DialogActive() || deviceLifecycle_.Active() ||
           instrumentLifecycle_.Active() || sampleEditor_.DialogActive() ||
           sampleSlices_.DialogActive() ||
           rename_.Active() || feedback_.Active();
  }
  [[nodiscard]] Ui2DialogSnapshot DialogSnapshot() const override {
    if (projectRender_.Active())
      return projectRender_.Snapshot();
    if (projectLifecycle_.Active())
      return projectLifecycle_.Snapshot();
    if (sampleBrowser_.DialogActive())
      return sampleBrowser_.DialogSnapshot();
    if (deviceLifecycle_.Active())
      return deviceLifecycle_.Snapshot();
    if (instrumentLifecycle_.Active())
      return instrumentLifecycle_.Snapshot();
    if (sampleEditor_.DialogActive())
      return sampleEditor_.DialogSnapshot();
    if (sampleSlices_.DialogActive())
      return sampleSlices_.DialogSnapshot();
    return rename_.Active() ? rename_.Snapshot() : feedback_.Snapshot();
  }
  [[nodiscard]] std::uint32_t DialogInstanceId() const override {
    // Modal owners need four tag bits. The owner tag prevents equal
    // local counters from making a newly opened Render/Firmware dialog look
    // like the previous modal instance.
    constexpr std::uint32_t tagBits = 4U;
    if (projectRender_.Active())
      return (projectRender_.InstanceId() << tagBits) | 3U;
    if (projectLifecycle_.Active())
      return (projectLifecycle_.InstanceId() << tagBits) | 1U;
    if (sampleBrowser_.DialogActive())
      return (sampleBrowser_.DialogInstanceId() << tagBits) | 2U;
    if (deviceLifecycle_.Active())
      return (deviceLifecycle_.InstanceId() << tagBits) | 4U;
    if (instrumentLifecycle_.Active())
      return (instrumentLifecycle_.InstanceId() << tagBits) | 5U;
    if (sampleEditor_.DialogActive())
      return (sampleEditor_.DialogInstanceId() << tagBits) | 7U;
    if (sampleSlices_.DialogActive())
      return (sampleSlices_.DialogInstanceId() << tagBits) | 8U;
    if (rename_.Active())
      return rename_.InstanceId() << tagBits;
    return (feedback_.InstanceId() << tagBits) | 6U;
  }

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
  void CaptureClipboardNotice(bool &active, bool &pasted,
                              std::uint8_t &width,
                              std::uint8_t &height) const;

  TrackerApplicationSession &session_;
  Ui2TrackerCommandExecutor &tracker_;
  Ui2ProjectController &project_;
  Ui2ProjectBrowserController &projectBrowser_;
  Ui2SettingsBrowserController &settingsBrowser_;
  Ui2ClipboardNoticeController &clipboardNotice_;
  Ui2FeedbackController &feedback_;
  Ui2ProjectLifecycleController &projectLifecycle_;
  Ui2ProjectRenderController &projectRender_;
  Ui2GrooveController &groove_;
  Ui2GrooveClipboard &grooveClipboard_;
  Ui2DeviceController &device_;
  Ui2DeviceLifecycleController &deviceLifecycle_;
  Ui2ThemeController &theme_;
  Ui2FontController &font_;
  Ui2RenameController &rename_;
  Ui2MixerController &mixer_;
  Ui2InstrumentController &instrument_;
  Ui2InstrumentLifecycleController &instrumentLifecycle_;
  Ui2InstrumentBrowserController &instrumentBrowser_;
  Ui2SampleBrowserController &sampleBrowser_;
  Ui2SampleEditorController &sampleEditor_;
  Ui2SampleSlicesController &sampleSlices_;
  Ui2RecordController &record_;
  FirmwareLifecycleService &firmwareLifecycle_;
  const Ui2PersistenceStatus &persistenceStatus_;
  UiApplicationPage activePage_ = UiApplicationPage::Song;
  bool navigationHeld_ = false;
  bool instrumentBrowserActive_ = false;
};

} // namespace ui2
