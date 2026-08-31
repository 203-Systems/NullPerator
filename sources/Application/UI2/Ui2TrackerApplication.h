/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Input/ITrackerInputSink.h"
#include "Application/Session/AutoSaveCoordinator.h"
#include "Application/Session/FirmwareLifecycleController.h"
#include "Application/Session/FirmwareLifecyclePlatformAdapter.h"
#include "Application/Session/FirmwareLifecycleService.h"
#include "Application/Session/TrackerApplicationSession.h"
#include "Application/UI2/Controllers/Ui2ControllerPrimitives.h"
#include "Application/UI2/Controllers/Ui2DeviceController.h"
#include "Application/UI2/Controllers/Ui2DeviceLifecycleController.h"
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
#include "Application/UI2/Controllers/Ui2RecordController.h"
#include "Application/UI2/Controllers/Ui2RenameController.h"
#include "Application/UI2/Controllers/Ui2SampleBrowserController.h"
#include "Application/UI2/Controllers/Ui2SampleEditorController.h"
#include "Application/UI2/Controllers/Ui2SampleSlicesController.h"
#include "Application/UI2/Controllers/Ui2SettingsBrowserController.h"
#include "Application/UI2/Controllers/Ui2ThemeController.h"
#include "Application/UI2/Controllers/Ui2TrackerControllerHub.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"
#include "Application/UI2/Ui2ConfigSaveState.h"
#include "Application/UI2/Ui2NativeApplicationStateSource.h"
#include "Application/UI2/Ui2PersistenceStatus.h"
#include "Application/UI2/Ui2ProjectRenderBackend.h"
#include "Application/UI2/Ui2SampleEditorTransaction.h"
#include "Application/UI2/Ui2SampleWaveformBackend.h"
#include "Application/UI2/Ui2StatusBridge.h"
#include "Application/UI2/Ui2TrackerSessionModelPort.h"
#include "Application/UI2/Workflows/Ui2GrooveWorkflow.h"
#include "Application/UI2/Workflows/Ui2ProjectWorkflow.h"

#include <array>
#include <type_traits>

namespace ui2 {

struct Ui2StartupOptions final {
  bool forceUntitledProject = false;
};

enum class Ui2DiagnosticBrowser : std::uint8_t {
  Project,
  Instrument,
  SampleImport,
  Theme,
};

static_assert(std::is_trivially_copyable_v<Ui2StartupOptions>);
static_assert(sizeof(Ui2StartupOptions) == 1U,
              "startup options must remain an embedded-friendly value type");

// The single native owner of UI2 input, controller state, model mutation and
// rendering. Platform adapters deliver TrackerAction values here directly;
// legacy AppWindow/View instances are intentionally absent from this graph.
class Ui2TrackerApplication final : public ITrackerInputSink {
public:
  explicit Ui2TrackerApplication(IUiPresenter &presenter);
  ~Ui2TrackerApplication();

  // Boot-button sampling is a platform responsibility. Passing the override
  // explicitly keeps the UI2-only product independent from legacy
  // Application.cpp and its process-global forceLoadUntitledProject flag.
  [[nodiscard]] bool Init(Ui2StartupOptions options = {});
  void Shutdown();
  void DispatchTrackerAction(TrackerAction action, bool pressed) override;
  [[nodiscard]] PresentResult Present();
  void Tick(std::uint32_t nowMs);
  void Invalidate() { runtime_.Invalidate(); }

  [[nodiscard]] TrackerApplicationSession &Session() { return session_; }
  [[nodiscard]] const TrackerApplicationSession &Session() const {
    return session_;
  }
  [[nodiscard]] Ui2NativeApplicationStateSource &StateSource() {
    return source_;
  }
  [[nodiscard]] const Ui2StatusBridge &StatusBridge() const {
    return statusBridge_;
  }
  [[nodiscard]] UiApplicationPage ActivePage() const { return activePage_; }
  bool ActivatePage(UiApplicationPage page);
  // Acceptance diagnostics enter real controller states through these typed
  // boundaries. Normal product navigation remains action-driven.
  bool ActivateDiagnosticTable(Ui2TrackerPage tablePage);
  bool ActivateDiagnosticBrowser(Ui2DiagnosticBrowser browser);

private:
  void DispatchLogicalAction(TrackerAction action, bool pressed);
  void DispatchPageAction(UiApplicationPage owner, TrackerAction action,
                          bool pressed);
  void SynchronizeNonGridNavigationHeld(bool held);
  [[nodiscard]] bool TryNavigate(TrackerAction action);
  void HandleProject(TrackerAction action, bool pressed);
  void HandleProjectLifecycle(TrackerAction action, bool pressed);
  void HandleBrowser(TrackerAction action, bool pressed);
  void HandleSampleBrowserDialog(TrackerAction action, bool pressed);
  void ExecuteSampleBrowser(Ui2SampleBrowserCommand command);
  void CloseSampleBrowser();
  void HandleSampleEditor(TrackerAction action, bool pressed);
  void HandleSampleEditorDialog(TrackerAction action, bool pressed);
  void ExecuteSampleEditor(Ui2SampleEditorCommand command);
  [[nodiscard]] bool CloseSampleEditor();
  [[nodiscard]] bool RecoverSampleEditorDestination();
  [[nodiscard]] bool ImportSampleToCurrentInstrument(const char *path,
                                                      const char *&error);
  void HandleSampleSlices(TrackerAction action, bool pressed);
  void ExecuteSampleSlices(Ui2SampleSlicesCommand command);
  [[nodiscard]] bool OpenSampleEditor(const char *path, bool projectPool,
                                      UiApplicationPage returnPage);
  [[nodiscard]] bool OpenSampleSlices(const char *path,
                                      UiApplicationPage returnPage);
  void StopSamplePreview();
  void UpdateSamplePreview(std::uint32_t nowMs);
  void SynchronizeSampleSlices();
  [[nodiscard]] UiApplicationPage BrowserReturnPage() const;
  void HandleGroove(TrackerAction action, bool pressed);
  void HandleDevice(TrackerAction action, bool pressed);
  void HandleDeviceLifecycle(TrackerAction action, bool pressed);
  void ExecuteDevice(Ui2DeviceCommand command);
  void HandleTheme(TrackerAction action, bool pressed);
  void ExecuteTheme(Ui2ThemeCommand command);
  void ApplyCurrentTheme();
  void CommitThemeName(const char *name, bool resetColors);
  void HandleFont(TrackerAction action, bool pressed);
  void HandleRename(TrackerAction action, bool pressed);
  void HandleMixer(TrackerAction action, bool pressed);
  void HandleInstrument(TrackerAction action, bool pressed);
  void HandleInstrumentLifecycle(TrackerAction action, bool pressed);
  void HandleRecord(TrackerAction action, bool pressed);
  void ExecuteRecord(Ui2RecordCommand command);
  void ConfigureRecordController();
  void ExecuteInstrument(Ui2InstrumentCommand command);
  void ExecuteInstrumentLifecycle(Ui2InstrumentLifecycleCommand command);
  void SaveCurrentInstrument(bool overwrite = false);
  void ExecuteProject(Ui2ProjectCommand command);
  void ExecuteProjectLifecycle(Ui2ProjectLifecycleCommand command);
  void SaveCurrentProject(bool overwrite = false);
  void ExecutePendingSave(std::uint32_t nowMs);
  void ExecuteGroove(Ui2GrooveCommand command);
  void SynchronizeGridPage();
  void ResetControllersAfterProjectBoundary();
  void MarkProjectDirty();
  void SynchronizeProjectMutationState();
  void ShowFeedbackError(const char *message);
  [[nodiscard]] bool FlushConfig();
  [[nodiscard]] bool AutosaveSafePage() const;
  [[nodiscard]] AutoSaveCoordinator::Conditions AutoSaveConditions() const;

  TrackerApplicationSession session_;
  Ui2TrackerSessionModelPort modelPort_;
  Ui2TrackerCommandExecutor tracker_;
  Ui2ProjectRenderBackend projectRenderBackend_;
  Ui2ProjectRenderController projectRender_;
  Ui2ProjectController project_{};
  Ui2ProjectBrowserController projectBrowser_{};
  Ui2SettingsBrowserController settingsBrowser_{};
  Ui2FeedbackController feedback_{};
  Ui2ProjectLifecycleController projectLifecycle_{};
  Ui2GrooveController groove_{};
  Ui2GrooveClipboard grooveClipboard_{};
  Ui2DeviceController device_{};
  Ui2DeviceLifecycleController deviceLifecycle_{};
  Ui2ThemeController theme_{};
  Ui2FontController font_{};
  Ui2RenameController rename_{};
  Ui2MixerController mixer_{};
  Ui2InstrumentController instrument_{};
  Ui2InstrumentLifecycleController instrumentLifecycle_{};
  Ui2InstrumentBrowserController instrumentBrowser_{};
  Ui2SampleBrowserController sampleBrowser_{};
  Ui2SampleWaveformBackend sampleWaveform_{};
  Ui2SampleEditorController sampleEditor_{sampleWaveform_};
  Ui2SampleEditorTransaction sampleEditorTransaction_{};
  Ui2SampleSlicesController sampleSlices_{sampleWaveform_};
  Ui2RecordController record_{};
  Ui2ControllerInputState projectInput_{};
  FirmwareLifecyclePlatformAdapter firmwarePlatform_{};
  FirmwareLifecycleService firmwareLifecycle_{&firmwarePlatform_};
  FirmwareLifecycleController firmwareController_{};
  Ui2PersistenceStatus persistenceStatus_{};
  Ui2NativeApplicationStateSource source_;
  UiApplicationRuntime runtime_;
  UiApplicationPage activePage_ = UiApplicationPage::Song;
  bool initialized_ = false;
  std::uint16_t physicalHeldMask_ = 0U;
  std::array<UiApplicationPage, static_cast<std::size_t>(TrackerAction::Count)>
      pressOwners_{};
  std::array<char, MAX_PROJECT_NAME_LENGTH + 1U> savedProjectName_{};
  bool projectSaveAsPending_ = false;
  Ui2DeferredProjectSave deferredProjectSave_{};
  enum class RenameTarget : std::uint8_t {
    None,
    Project,
    Instrument,
    Theme,
    NewTheme,
  };
  RenameTarget renameTarget_ = RenameTarget::None;
  bool instrumentBrowserActive_ = false;
  UiApplicationPage sampleReturnPage_ = UiApplicationPage::Instrument;
  enum class SamplePreviewKind : std::uint8_t {
    None,
    EditorStream,
    SliceNote,
  };
  SamplePreviewKind samplePreviewKind_ = SamplePreviewKind::None;
  std::uint32_t samplePreviewStartedMs_ = 0U;
  std::uint32_t samplePreviewStart_ = 0U;
  std::uint32_t samplePreviewEnd_ = 0U;
  std::uint32_t samplePreviewFrames_ = 0U;
  std::uint32_t samplePreviewRate_ = 0U;
  std::uint8_t samplePreviewInstrument_ = 0U;
  std::uint8_t samplePreviewNote_ = 0U;
  bool samplePreviewSingleCycle_ = false;
  std::uint8_t renameInstrumentNumber_ = 0U;
  Ui2ConfigSaveState configSave_{};
  AutoSaveCoordinator autoSave_{};
  enum class PendingSaveKind : std::uint8_t {
    None,
    Project,
    AutoSave,
  };
  PendingSaveKind pendingSave_ = PendingSaveKind::None;
  bool pendingSaveOverwrite_ = false;
  std::uint32_t observedProjectMutationGeneration_ = 0U;
  Ui2StatusBridge statusBridge_{};
};

} // namespace ui2
