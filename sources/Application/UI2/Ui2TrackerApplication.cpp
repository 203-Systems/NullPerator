/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2TrackerApplication.h"
#include "Application/UI2/Ui2BrightnessMapping.h"
#include "Application/UI2/Ui2DeviceLifecycleService.h"
#include "Application/UI2/Ui2GrooveCommandAdapter.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Application/UI2/Ui2InstrumentTableAllocation.h"
#include "Application/UI2/Ui2ProjectNamePresentation.h"
#include "Application/UI2/Ui2SampleFileOperations.h"
#include "Application/UI2/Ui2TransportPolicy.h"
#include "Application/UI2/Workflows/Ui2FontWorkflow.h"
#include "Application/UI2/Workflows/Ui2InstrumentWorkflow.h"
#include "Application/UI2/Workflows/Ui2SampleEditorSaveWorkflow.h"
#include "Application/UI2/Workflows/Ui2ThemeWorkflow.h"

#include "Application/Audio/RecordingPlatform.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SoundSource.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Model/Config.h"
#include "Application/Model/Groove.h"
#include "Application/Model/Scale.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/Player.h"
#include "Services/Audio/Audio.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ui2 {
namespace {

// Project is a PersistencyService sub-service. The service must exist before
// TrackerApplicationSession constructs its Project member, otherwise the
// Project has nothing to register with and a later Load() can successfully
// parse the file while restoring none of the project data.
const char *InstallPersistenceBeforeSession() {
  PersistencyService::GetInstance();
  return UNNAMED_PROJECT_NAME;
}

void EnsureDirectory(FileSystem *fileSystem, const char *path) {
  if (fileSystem != nullptr && !fileSystem->exists(path))
    fileSystem->makeDir(path);
}

Ui2TrackerPage TrackerPageFor(UiApplicationPage page) {
  switch (page) {
  case UiApplicationPage::Song:
    return Ui2TrackerPage::Song;
  case UiApplicationPage::Chain:
    return Ui2TrackerPage::Chain;
  case UiApplicationPage::Phrase:
    return Ui2TrackerPage::Phrase;
  case UiApplicationPage::Table:
    return Ui2TrackerPage::PhraseTable;
  case UiApplicationPage::Instrument:
    return Ui2TrackerPage::Instrument;
  case UiApplicationPage::Project:
    return Ui2TrackerPage::Project;
  case UiApplicationPage::Groove:
    return Ui2TrackerPage::Groove;
  case UiApplicationPage::Mixer:
    return Ui2TrackerPage::Mixer;
  case UiApplicationPage::Record:
    return Ui2TrackerPage::Record;
  case UiApplicationPage::None:
  case UiApplicationPage::Device:
  case UiApplicationPage::Theme:
  case UiApplicationPage::Font:
  case UiApplicationPage::Browser:
  case UiApplicationPage::SampleEditor:
  case UiApplicationPage::SampleSlices:
    return Ui2TrackerPage::None;
  }
  return Ui2TrackerPage::None;
}

void StartSongTransport(TrackerApplicationSession &session) {
  Ui2ToggleSongTransportAtCursor(
      *Player::GetInstance(), PM_SONG, session.EditorState().songX_,
      static_cast<std::uint8_t>(SONG_CHANNEL_COUNT));
}

SampleInstrument *CurrentSampleInstrument(TrackerApplicationSession &session) {
  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  if (bank == nullptr)
    return nullptr;
  const auto slot = static_cast<unsigned short>(
      std::clamp(session.EditorState().currentInstrumentID_, 0,
                 MAX_INSTRUMENT_COUNT - 1));
  I_Instrument *instrument = bank->GetInstrument(slot);
  return instrument != nullptr && instrument->GetType() == IT_SAMPLE
             ? static_cast<SampleInstrument *>(instrument)
             : nullptr;
}

Ui2InstrumentParameterDescriptor ActiveInstrumentParameter(
    TrackerApplicationSession &session,
    Ui2InstrumentCursorPosition cursor) {
  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  if (bank == nullptr)
    return {};
  const auto slot = static_cast<unsigned short>(
      std::clamp(session.EditorState().currentInstrumentID_, 0,
                 MAX_INSTRUMENT_COUNT - 1));
  I_Instrument *instrument = bank->GetInstrument(slot);
  if (instrument == nullptr)
    return {};
  const InstrumentType type = instrument->GetType();
  const bool sidFirstChip =
      type != IT_SID || static_cast<SIDInstrument *>(instrument)->GetChip() == SID1;
  Ui2InstrumentParameterDescriptor descriptor =
      Ui2InstrumentCursorParameter(type, cursor, sidFirstChip);
  if (type == IT_SAMPLE && Ui2IsSamplePositionParameter(descriptor)) {
    descriptor = Ui2ResolveSamplePositionMaximum(
        descriptor, static_cast<SampleInstrument *>(instrument)->GetSampleSize());
  }
  return descriptor;
}

void ConfigureInstrumentSubfields(TrackerApplicationSession &session,
                                  Ui2InstrumentController &controller) {
  const Ui2InstrumentSubfieldSpec spec = Ui2InstrumentSubfields(
      ActiveInstrumentParameter(session, controller.Cursor()));
  controller.ConfigureValueSubfields(spec.mode, spec.count);
}

TrackerAction InstrumentTypeChangeTrigger(
    Ui2InstrumentValueDirection direction) {
  if (direction == Ui2InstrumentValueDirection::Left)
    return TrackerAction::Left;
  if (direction == Ui2InstrumentValueDirection::Right)
    return TrackerAction::Right;
  return TrackerAction::Count;
}

} // namespace

Ui2TrackerApplication::Ui2TrackerApplication(IUiPresenter &presenter)
    : session_(InstallPersistenceBeforeSession()), modelPort_(session_),
      tracker_(modelPort_),
      projectRenderBackend_(session_.ProjectModel(), session_.EditorState()),
      projectRender_(projectRenderBackend_),
      source_(session_, tracker_, project_, projectBrowser_, settingsBrowser_,
              clipboardNotice_, feedback_, projectLifecycle_, projectRender_,
              groove_, grooveClipboard_, device_, deviceLifecycle_, theme_,
              font_, rename_, mixer_, instrument_,
              instrumentLifecycle_, instrumentBrowser_, sampleBrowser_,
              sampleEditor_, sampleSlices_, record_, firmwareLifecycle_,
              persistenceStatus_),
      runtime_(presenter) {}

Ui2TrackerApplication::~Ui2TrackerApplication() { Shutdown(); }

bool Ui2TrackerApplication::Init(Ui2StartupOptions options) {
  // UI2-only products do not construct AppWindow, which historically owned
  // the process-global Status sink. Install a fixed-capacity capture boundary
  // before loading any project data so progress/errors remain observable by a
  // platform or a future approved UI treatment instead of disappearing.
  statusBridge_.Attach();
  statusBridge_.Clear();
  persistenceStatus_.Reset();
  pendingSave_ = PendingSaveKind::None;
  pendingSaveOverwrite_ = false;
  deferredProjectSave_.Cancel();

  FileSystem *fileSystem = FileSystem::GetInstance();
  EnsureDirectory(fileSystem, PROJECTS_DIR);
  EnsureDirectory(fileSystem, SAMPLES_LIB_DIR);
  EnsureDirectory(fileSystem, INSTRUMENTS_DIR);
  EnsureDirectory(fileSystem, RENDERS_DIR);
  EnsureDirectory(fileSystem, THEMES_DIR);
  EnsureDirectory(fileSystem, RECORDINGS_DIR);

  // The boot override deliberately clears both recovery locations before any
  // current-project lookup. The typed result is retained at the service
  // boundary; visual failure feedback waits for an approved startup/error UI.
  (void)firmwareLifecycle_.PrepareProjectBoot(options.forceUntitledProject);

  // Legacy AppWindow ignored MIDI startup failure and continued with audio and
  // project recovery. Preserve that data-safe behavior until a dedicated
  // startup error state is approved, while still giving UI2 deterministic
  // ownership and teardown of the service.
  (void)firmwareLifecycle_.InitializeMidi();

  // Player::Init(), reached from LoadProject(), starts its mixer. Install and
  // initialize the platform audio service first so Player does not become
  // permanently not-ready merely because UI2 loaded the project too early.
  Audio::GetInstance()->Init();

  char projectName[MAX_PROJECT_NAME_LENGTH + 1U]{};
  bool createProject = false;
  if (PersistencyService::GetInstance()->LoadCurrentProjectName(projectName) !=
      PERSIST_LOADED) {
    std::strncpy(projectName, UNNAMED_PROJECT_NAME, sizeof(projectName) - 1U);
    createProject = true;
  }
  const bool sampleDeletesRecovered =
      createProject ||
      (fileSystem != nullptr &&
       Ui2RecoverStagedProjectSampleDeletes(*fileSystem, projectName));
  if (!sampleDeletesRecovered ||
      session_.LoadProject(projectName, createProject) !=
          TrackerApplicationSession::LoadResult::Loaded) {
    if (std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0 ||
        session_.LoadProject(UNNAMED_PROJECT_NAME, true) !=
            TrackerApplicationSession::LoadResult::Loaded) {
      Shutdown();
      return false;
    }
  }
  std::snprintf(savedProjectName_.data(), savedProjectName_.size(), "%s",
                session_.ProjectName());

  std::uint16_t brightnessPercent = 100U;
  std::uint16_t midiDevice = 0U;
  std::uint16_t midiSync = 0U;
  std::uint16_t resampler = 0U;
  std::uint16_t lineOut = 2U;
  std::uint16_t volume = 40U;
  if (Config *config = Config::GetInstance()) {
    const auto configValue = [config](FourCC key, int fallback, int maximum) {
      if (Variable *value = config->FindVariable(key))
        return std::clamp(value->GetInt(), 0, maximum);
      return fallback;
    };
    midiDevice =
        static_cast<std::uint16_t>(configValue(FourCC::VarMidiDevice, 0, 3));
    midiSync =
        static_cast<std::uint16_t>(configValue(FourCC::VarMidiSync, 0, 1));
    resampler = static_cast<std::uint16_t>(
        configValue(FourCC::VarImportResampler, 0, 1));
    lineOut = static_cast<std::uint16_t>(configValue(FourCC::VarLineOut, 2, 2));
    volume = static_cast<std::uint16_t>(
        configValue(FourCC::VarOutputVolume, 40, 100));
    font_.SetTextCase(
        static_cast<std::uint8_t>(configValue(
            FourCC::VarUITextCase, 1, Ui2FontController::TextCaseCount - 1U)));
    if (Variable *brightness =
            config->FindVariable(FourCC::VarBacklightLevel)) {
      const int configuredBrightness = brightness->GetInt();
      const int rawBrightness = std::clamp(
          configuredBrightness,
          static_cast<int>(Ui2MinimumVisibleBrightness), 0xFF);
      brightnessPercent = Ui2BrightnessPercentFromRaw(rawBrightness);
      if (configuredBrightness != rawBrightness) {
        brightness->SetInt(rawBrightness);
        configSave_.MarkDirty();
      }
      System::GetInstance()->SetDisplayBrightness(
          static_cast<unsigned char>(rawBrightness));
    }
  }

  ApplyCurrentTheme();

  initialized_ = true;
  const std::uint32_t nowMs = System::GetInstance()->Millis();
  autoSave_.OnProjectLoaded(nowMs);
  observedProjectMutationGeneration_ = modelPort_.ProjectMutationGeneration();
  device_.SetSelector(Ui2DeviceField::MidiDevice, {4U, midiDevice, true});
  device_.SetSelector(Ui2DeviceField::MidiSync, {2U, midiSync, false});
  device_.SetSelector(Ui2DeviceField::Resampler, {2U, resampler, true});
  device_.SetSelector(Ui2DeviceField::LineOut, {3U, lineOut, true});
  device_.SetSelector(Ui2DeviceField::Volume, {101U, volume, false});
  device_.SetSelector(Ui2DeviceField::Brightness,
                      {101U, brightnessPercent, false});
  std::uint32_t visibleDeviceFields = Ui2DeviceController::AllFieldsMask;
  visibleDeviceFields &=
      ~(std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::LineOut));
  // Node hardware does not expose the bootloader action; other firmware
  // targets retain the guarded UPDATE FIRMWARE row.
  visibleDeviceFields &=
      ~(std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::UpdateFirmware));
#if defined(NULLPERATOR_IOS)
  visibleDeviceFields &=
      ~(std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::MidiDevice));
  visibleDeviceFields &=
      ~(std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::Volume));
  visibleDeviceFields &=
      ~(std::uint32_t{1}
        << static_cast<std::uint8_t>(Ui2DeviceField::Brightness));
#endif
  device_.SetVisibleFields(visibleDeviceFields);
  ConfigureRecordController();
  ActivatePage(UiApplicationPage::Song);
  return true;
}

void Ui2TrackerApplication::Shutdown() {
  deferredProjectSave_.Cancel();
  // Shutdown is also called directly by host/adapter teardown, without a page
  // transition. Do not drop coalesced settings just because that path never
  // reached ActivatePage().
  if (pendingSave_ != PendingSaveKind::None) {
    System *system = System::GetInstance();
    ExecutePendingSave(system == nullptr ? 0U : system->Millis());
  }
  if (configSave_.Dirty())
    (void)FlushConfig();
  StopSamplePreview();
  if (IsRecordingActive())
    StopRecording();
  StopMonitoring();
  if (sampleEditorTransaction_.Active())
    (void)sampleEditorTransaction_.Discard();
  sampleEditorTransaction_.Reset();
  sampleEditor_.Close();
  if (session_.IsLoaded())
    session_.CloseProject();
  (void)firmwareLifecycle_.CloseMidi();
  initialized_ = false;
  statusBridge_.Detach();
}

void Ui2TrackerApplication::DispatchTrackerAction(TrackerAction action,
                                                  bool pressed) {
  if (!initialized_ || !TrackerActionIsValid(action))
    return;

  const std::uint16_t bit = TrackerActionBit(action);
  const bool acceptInput =
      Ui2AcceptInputEvent(action, pressed, physicalHeldMask_);
  if (pressed)
    physicalHeldMask_ |= bit;
  else
    physicalHeldMask_ &= static_cast<std::uint16_t>(~bit);

  if (!acceptInput)
    return;

  if (action == TrackerAction::Power) {
    System *system = System::GetInstance();
    firmwareController_.SetPowerButton(
        pressed, system == nullptr ? 0U : system->Millis());
    // POWER is a firmware lifecycle input, never a page/controller command.
    return;
  }

  if (action == TrackerAction::Shift) {
    SynchronizeNonGridNavigationHeld(pressed);
    source_.SetNavigationHeld(pressed);
    tracker_.Hub().SetNavigationHeld(pressed);
  }

  DispatchLogicalAction(action, pressed);
}

void Ui2TrackerApplication::SynchronizeNonGridNavigationHeld(bool held) {
  // Navigation may activate another page before SHIFT is released. Share the
  // physical latch only: each controller still receives commands exclusively
  // through DispatchPageAction and its original press owner.
  projectInput_.SetNavigationHeld(held);
  device_.SetNavigationHeld(held);
  theme_.SetNavigationHeld(held);
  font_.SetNavigationHeld(held);
  groove_.SetNavigationHeld(held);
  mixer_.SetNavigationHeld(held);
  instrument_.SetNavigationHeld(held);
  sampleBrowser_.SetNavigationHeld(held);
}

void Ui2TrackerApplication::DispatchLogicalAction(TrackerAction action,
                                                  bool pressed) {
  const std::size_t actionIndex = static_cast<std::size_t>(action);
  const UiApplicationPage releaseOwner =
      !pressed ? pressOwners_[actionIndex] : UiApplicationPage::None;
  const auto finishModalRelease = [this, action, pressed, actionIndex,
                                   releaseOwner]() {
    if (pressed)
      return;
    pressOwners_[actionIndex] = UiApplicationPage::None;
    // A modal consumes its own key-up, but the controller that accepted the
    // corresponding key-down must also see that release. Otherwise a chord
    // such as Project Browser OPTION+ENTER can leave OPTION latched after its
    // confirmation closes and turn the next plain ENTER into another delete.
    if (releaseOwner != UiApplicationPage::None)
      DispatchPageAction(releaseOwner, action, false);
  };
  if (projectRender_.Active()) {
    projectRender_.Handle(action, pressed);
    finishModalRelease();
    return;
  }
  if (projectLifecycle_.Active()) {
    HandleProjectLifecycle(action, pressed);
    finishModalRelease();
    return;
  }
  if (sampleBrowser_.DialogActive()) {
    HandleSampleBrowserDialog(action, pressed);
    finishModalRelease();
    return;
  }
  if (sampleEditor_.DialogActive()) {
    HandleSampleEditorDialog(action, pressed);
    finishModalRelease();
    return;
  }
  if (sampleSlices_.DialogActive()) {
    HandleSampleSlicesDialog(action, pressed);
    finishModalRelease();
    return;
  }
  if (deviceLifecycle_.Active()) {
    HandleDeviceLifecycle(action, pressed);
    finishModalRelease();
    return;
  }
  if (instrumentLifecycle_.Active()) {
    HandleInstrumentLifecycle(action, pressed);
    finishModalRelease();
    return;
  }
  if (rename_.Active()) {
    HandleRename(action, pressed);
    finishModalRelease();
    return;
  }
  if (pressed &&
      (physicalHeldMask_ & TrackerActionBit(TrackerAction::Shift)) != 0U) {
    const UiApplicationPage navigationOwner = activePage_;
    if (TryNavigate(action)) {
      (void)Ui2ClaimPressOwner(pressOwners_[actionIndex], navigationOwner,
                               UiApplicationPage::None);
      return;
    }
  }

  const UiApplicationPage owner =
      pressed ? Ui2ClaimPressOwner(pressOwners_[actionIndex], activePage_,
                                   UiApplicationPage::None)
              : Ui2ReleasePressOwner(pressOwners_[actionIndex], activePage_,
                                     UiApplicationPage::None);

  DispatchPageAction(owner, action, pressed);
}

void Ui2TrackerApplication::DispatchPageAction(UiApplicationPage owner,
                                               TrackerAction action,
                                               bool pressed) {
  switch (owner) {
  case UiApplicationPage::Song:
  case UiApplicationPage::Chain:
  case UiApplicationPage::Phrase:
  case UiApplicationPage::Table: {
    const Ui2TrackerCommandBatch<> batch = tracker_.Handle(action, pressed);
    ShowTrackerClipboardNotice(batch);
    SynchronizeProjectMutationState();
    SynchronizeGridPage();
    break;
  }
  case UiApplicationPage::Project:
    HandleProject(action, pressed);
    break;
  case UiApplicationPage::Groove:
    HandleGroove(action, pressed);
    break;
  case UiApplicationPage::Device:
    HandleDevice(action, pressed);
    break;
  case UiApplicationPage::Mixer:
    HandleMixer(action, pressed);
    break;
  case UiApplicationPage::Instrument:
    HandleInstrument(action, pressed);
    break;
  case UiApplicationPage::SampleEditor:
    HandleSampleEditor(action, pressed);
    break;
  case UiApplicationPage::SampleSlices:
    HandleSampleSlices(action, pressed);
    break;
  case UiApplicationPage::None:
    break;
  case UiApplicationPage::Record:
    HandleRecord(action, pressed);
    break;
  case UiApplicationPage::Browser:
    HandleBrowser(action, pressed);
    break;
  case UiApplicationPage::Theme:
    HandleTheme(action, pressed);
    break;
  case UiApplicationPage::Font:
    HandleFont(action, pressed);
    break;
  }
}

bool Ui2TrackerApplication::TryNavigate(TrackerAction action) {
  if (action != TrackerAction::Left && action != TrackerAction::Right &&
      action != TrackerAction::Up && action != TrackerAction::Down)
    return false;
  const std::uint16_t modifiers = TrackerActionBit(TrackerAction::Option) |
                                  TrackerActionBit(TrackerAction::Enter);
  if ((physicalHeldMask_ & modifiers) != 0U)
    return false;

  UiApplicationPage target = UiApplicationPage::None;
  Ui2TrackerPage trackerTarget = Ui2TrackerPage::None;
  switch (activePage_) {
  case UiApplicationPage::Song:
    if (action == TrackerAction::Right)
      target = UiApplicationPage::Chain;
    else if (action == TrackerAction::Up)
      target = UiApplicationPage::Project;
    else if (action == TrackerAction::Down)
      target = UiApplicationPage::Mixer;
    break;
  case UiApplicationPage::Chain:
    if (action == TrackerAction::Left)
      target = UiApplicationPage::Song;
    else if (action == TrackerAction::Right)
      target = UiApplicationPage::Phrase;
    break;
  case UiApplicationPage::Phrase:
    if (action == TrackerAction::Left)
      target = UiApplicationPage::Chain;
    else if (action == TrackerAction::Right)
      target = UiApplicationPage::Instrument;
    else if (action == TrackerAction::Down) {
      target = UiApplicationPage::Table;
      trackerTarget = Ui2TrackerPage::PhraseTable;
    } else if (action == TrackerAction::Up)
      target = UiApplicationPage::Groove;
    break;
  case UiApplicationPage::Instrument:
    if (action == TrackerAction::Left)
      target = UiApplicationPage::Phrase;
    else if (action == TrackerAction::Down) {
      target = UiApplicationPage::Table;
      trackerTarget = Ui2TrackerPage::InstrumentTable;
    }
    break;
  case UiApplicationPage::Table:
    if (action == TrackerAction::Up)
      target = tracker_.Hub().ActivePage() == Ui2TrackerPage::InstrumentTable
                   ? UiApplicationPage::Instrument
                   : UiApplicationPage::Phrase;
    else if (action == TrackerAction::Left &&
             tracker_.Hub().ActivePage() == Ui2TrackerPage::InstrumentTable) {
      target = UiApplicationPage::Table;
      trackerTarget = Ui2TrackerPage::PhraseTable;
    } else if (action == TrackerAction::Right &&
               tracker_.Hub().ActivePage() == Ui2TrackerPage::PhraseTable) {
      target = UiApplicationPage::Table;
      trackerTarget = Ui2TrackerPage::InstrumentTable;
    }
    break;
  case UiApplicationPage::Groove:
    if (action == TrackerAction::Down)
      target = UiApplicationPage::Phrase;
    break;
  case UiApplicationPage::Mixer:
    if (action == TrackerAction::Up)
      target = UiApplicationPage::Song;
    break;
  case UiApplicationPage::Project:
    if (action == TrackerAction::Down)
      target = UiApplicationPage::Song;
    else if (action == TrackerAction::Up)
      target = UiApplicationPage::Device;
    break;
  case UiApplicationPage::Device:
    if (action == TrackerAction::Down)
      target = UiApplicationPage::Project;
    break;
  case UiApplicationPage::Theme:
  case UiApplicationPage::Font:
    if (action == TrackerAction::Left)
      target = UiApplicationPage::Device;
    break;
  case UiApplicationPage::Browser:
    if (action == TrackerAction::Left)
      target = BrowserReturnPage();
    break;
  case UiApplicationPage::SampleEditor:
  case UiApplicationPage::SampleSlices:
    if (action == TrackerAction::Left)
      target = sampleReturnPage_;
    break;
  case UiApplicationPage::Record:
  case UiApplicationPage::None:
    break;
  }
  if (target == UiApplicationPage::None)
    return true;

  if (activePage_ == UiApplicationPage::Project &&
      target != UiApplicationPage::Project && projectSaveAsPending_) {
    projectLifecycle_.WarnPendingRename();
    return true;
  }

  const Ui2TrackerPage sourcePage = tracker_.Hub().ActivePage();
  const Ui2TrackerPage destinationPage = trackerTarget != Ui2TrackerPage::None
                                             ? trackerTarget
                                             : TrackerPageFor(target);
  const Ui2TrackerActiveControllerState controller = tracker_.ActiveState();
  if (destinationPage != Ui2TrackerPage::None &&
      !modelPort_.PreparePageNavigation(sourcePage, destinationPage,
                                        controller.track, controller.row))
    return true;
  tracker_.SynchronizeFromPort();

  if (activePage_ == UiApplicationPage::Browser &&
      target != UiApplicationPage::Browser) {
    settingsBrowser_.Close();
    CloseSampleBrowser();
    if (instrumentBrowserActive_) {
      instrumentBrowserActive_ = false;
      source_.SetInstrumentBrowserActive(false);
    }
  }
  ActivatePage(target);
  if (trackerTarget != Ui2TrackerPage::None) {
    tracker_.Hub().Activate(trackerTarget);
    modelPort_.StoreGridState(tracker_.Hub().State());
  }
  return true;
}

PresentResult Ui2TrackerApplication::Present() {
  if (!initialized_)
    return PresentResult::Failed;
  const PresentResult result = runtime_.Present(source_);
  if (result == PresentResult::Presented)
    persistenceStatus_.MarkPresented();
  return result;
}

void Ui2TrackerApplication::Tick(std::uint32_t nowMs) {
  if (!initialized_)
    return;
  persistenceStatus_.Tick(nowMs);
  if (feedback_.Tick(nowMs))
    runtime_.Invalidate();
  if (clipboardNotice_.Tick(nowMs))
    runtime_.Invalidate();
  const FirmwareLifecycleCommand firmwareCommand =
      firmwareLifecycle_.Tick(firmwareController_, nowMs);
  // A physical power-down bypasses the normal page-boundary/config flush.
  // Give pending UI settings one final synchronous persistence attempt before
  // the platform cuts power; failure remains dirty and is surfaced through
  // Status, but must not make a device with broken storage impossible to turn
  // off.
  if (firmwareCommand.HasValue() && configSave_.Dirty())
    (void)FlushConfig();
  (void)firmwareLifecycle_.Execute(firmwareCommand);
  projectRender_.Tick();
  TickRecordLifecycle();
  TickSampleEditorApply();
  UpdateSamplePreview(nowMs);
  SynchronizeProjectMutationState();
  if (pendingSave_ != PendingSaveKind::None) {
    if (persistenceStatus_.ReadyToPersist())
      ExecutePendingSave(nowMs);
    return;
  }
  const AutoSaveCoordinator::Conditions conditions = AutoSaveConditions();
  if (autoSave_.Tick(nowMs, conditions) !=
      AutoSaveCoordinator::TickResult::SaveRequested)
    return;
  pendingSave_ = PendingSaveKind::AutoSave;
  persistenceStatus_.BeginSaving();
}

bool Ui2TrackerApplication::AutosaveSafePage() const {
  switch (activePage_) {
  case UiApplicationPage::Song:
  case UiApplicationPage::Chain:
  case UiApplicationPage::Phrase:
  case UiApplicationPage::Table:
  case UiApplicationPage::Groove:
  case UiApplicationPage::Instrument:
  case UiApplicationPage::Device:
  case UiApplicationPage::Theme:
  case UiApplicationPage::Mixer:
    return !rename_.Active();
  case UiApplicationPage::Project:
  case UiApplicationPage::Font:
  case UiApplicationPage::Browser:
  case UiApplicationPage::SampleEditor:
  case UiApplicationPage::SampleSlices:
  case UiApplicationPage::Record:
  case UiApplicationPage::None:
    return false;
  }
  return false;
}

AutoSaveCoordinator::Conditions
Ui2TrackerApplication::AutoSaveConditions() const {
  return {
      .projectLoaded = session_.IsLoaded(),
      .playerRunning = Player::GetInstance()->IsRunning(),
      .recordingActive = IsRecordingActive() || IsSavingRecording(),
      .operationAllowsSave = AutosaveSafePage() &&
                             !projectRender_.Active() &&
                             !projectLifecycle_.Active() &&
                             !deviceLifecycle_.Active() &&
                             !instrumentLifecycle_.Active() &&
                             !rename_.Active(),
  };
}

bool Ui2TrackerApplication::ActivatePage(UiApplicationPage page) {
  if (page == UiApplicationPage::None)
    return false;
  if (page != activePage_ && sampleEditorTransaction_.ApplyActive())
    return false;
  if (activePage_ == UiApplicationPage::Project &&
      page != UiApplicationPage::Project && projectSaveAsPending_) {
    projectLifecycle_.WarnPendingRename();
    return false;
  }
  const bool changed = activePage_ != page;
  if (changed && activePage_ == UiApplicationPage::Record &&
      (IsRecordingActive() || IsSavingRecording())) {
    ShowFeedbackMessage("STOP RECORDING FIRST");
    return false;
  }
  if (changed)
    deferredProjectSave_.Cancel();
  if (changed && activePage_ == UiApplicationPage::SampleEditor &&
      page != UiApplicationPage::SampleEditor && sampleEditor_.Active()) {
    if (!CloseSampleEditor())
      return false;
  }
  if (changed && activePage_ == UiApplicationPage::SampleSlices &&
      page != UiApplicationPage::SampleSlices && sampleSlices_.Active()) {
    StopSamplePreview();
    sampleSlices_.Close();
  }
  if (changed && activePage_ == UiApplicationPage::Record)
    StopMonitoring();
  // Config writes are coalesced until a page boundary. A failed Sync leaves
  // the one-byte retry state dirty, so returning to or leaving any settings
  // page later retries the complete config instead of silently losing edits.
  if (changed && configSave_.Dirty())
    (void)FlushConfig();
  activePage_ = page;
  source_.SetActivePage(page);
  if (page == UiApplicationPage::Mixer)
    mixer_.Synchronize(static_cast<std::uint8_t>(
        std::clamp(session_.EditorState().songX_, 0, SONG_CHANNEL_COUNT - 1)));
  if (page == UiApplicationPage::Record) {
    Config *config = Config::GetInstance();
    const auto value = [config](FourCC key) {
      Variable *variable =
          config == nullptr ? nullptr : config->FindVariable(key);
      return variable == nullptr ? 0 : variable->GetInt();
    };
    record_.Synchronize(
        static_cast<std::uint8_t>(value(FourCC::VarRecordSource)));
    SetInputSource(static_cast<RecordSource>(
        std::clamp(value(FourCC::VarRecordSource), 0, 2)));
    StartMonitoring();
  }
  const Ui2TrackerPage trackerPage = TrackerPageFor(page);
  if (trackerPage != Ui2TrackerPage::None) {
    tracker_.Hub().Activate(trackerPage);
    modelPort_.StoreGridState(tracker_.Hub().State());
  }
  if (changed)
    runtime_.Invalidate();
  return changed;
}

bool Ui2TrackerApplication::ActivateDiagnosticTable(
    Ui2TrackerPage tablePage) {
  if (tablePage != Ui2TrackerPage::PhraseTable &&
      tablePage != Ui2TrackerPage::InstrumentTable)
    return false;
  (void)ActivatePage(UiApplicationPage::Table);
  if (activePage_ != UiApplicationPage::Table)
    return false;
  if (tracker_.Hub().Activate(tablePage)) {
    modelPort_.StoreGridState(tracker_.Hub().State());
    runtime_.Invalidate();
  }
  return true;
}

bool Ui2TrackerApplication::ActivateDiagnosticBrowser(
    Ui2DiagnosticBrowser browser) {
  (void)ActivatePage(UiApplicationPage::Browser);
  if (activePage_ != UiApplicationPage::Browser)
    return false;

  settingsBrowser_.Close();
  CloseSampleBrowser();
  instrumentBrowserActive_ = false;
  source_.SetInstrumentBrowserActive(false);

  switch (browser) {
  case Ui2DiagnosticBrowser::Project:
    // The project controller remains the browser source even when refreshing
    // an unavailable directory produces an empty diagnostic state, but the
    // storage failure must not look like a genuinely empty project library.
    if (!projectBrowser_.Refresh(session_.ProjectName()))
      projectLifecycle_.ReportFailure(
          Ui2ProjectLifecycleFailure::OpenProjectBrowser);
    break;
  case Ui2DiagnosticBrowser::Instrument:
    if (!instrumentBrowser_.Refresh())
      instrumentBrowser_.SetError("INSTRUMENT BROWSER FAILED");
    instrumentBrowserActive_ = true;
    source_.SetInstrumentBrowserActive(true);
    break;
  case Ui2DiagnosticBrowser::SampleImport: {
    if (!sampleBrowser_.OpenLibrary(session_.ProjectName()))
      return false;
    break;
  }
  case Ui2DiagnosticBrowser::Theme: {
    std::array<char, MAX_THEME_NAME_LENGTH + 1U> currentName{};
    if (Config *config = Config::GetInstance()) {
      if (Variable *name = config->FindVariable(FourCC::VarThemeName))
        std::snprintf(currentName.data(), currentName.size(), "%s",
                      name->GetString().c_str());
    }
    (void)settingsBrowser_.OpenTheme(currentName.data());
    break;
  }
  }

  // Consecutive diagnostic browser requests keep the same application page,
  // so the controller-mode change itself must invalidate the native frame.
  runtime_.Invalidate();
  return true;
}

void Ui2TrackerApplication::HandleProject(TrackerAction action, bool pressed) {
  if (!projectInput_.Update(action, pressed))
    return;
  project_.SetEnterHeld(projectInput_.Held(TrackerAction::Enter));
  if (!pressed)
    return;

  if (projectInput_.Held(TrackerAction::Shift) ||
      projectInput_.Held(TrackerAction::Option)) {
    return;
  }
  if (projectInput_.Held(TrackerAction::Enter)) {
    if (action == TrackerAction::Enter)
      ExecuteProject(project_.Enter());
    else if ((project_.ContentCursor() == Ui2ProjectContentCursor::Tempo ||
              project_.ContentCursor() == Ui2ProjectContentCursor::Transpose ||
              project_.ContentCursor() == Ui2ProjectContentCursor::Scale ||
              project_.ContentCursor() == Ui2ProjectContentCursor::Root) &&
             (action == TrackerAction::Left || action == TrackerAction::Right ||
              action == TrackerAction::Up || action == TrackerAction::Down))
      ExecuteProject(project_.Adjust(action));
    return;
  }
  if (projectInput_.AnyModifier())
    return;

  // Value rows always expose fine horizontal editing. Holding ENTER adds the
  // vertical coarse path, but is not a prerequisite for ordinary +/-1.
  if (action == TrackerAction::Left || action == TrackerAction::Right) {
    const Ui2ProjectCommand adjustment = project_.Adjust(action);
    if (adjustment.HasValue()) {
      ExecuteProject(adjustment);
      return;
    }
  }

  switch (action) {
  case TrackerAction::Up:
    project_.MoveUp();
    break;
  case TrackerAction::Down:
    project_.MoveDown();
    break;
  case TrackerAction::Left:
    project_.MoveLeft();
    break;
  case TrackerAction::Right:
    project_.MoveRight();
    break;
  case TrackerAction::Play:
    StartSongTransport(session_);
    break;
  case TrackerAction::Shift:
  case TrackerAction::Option:
  case TrackerAction::Enter:
  case TrackerAction::Power:
  default:
    break;
  }
}

void Ui2TrackerApplication::HandleBrowser(TrackerAction action, bool pressed) {
  if (settingsBrowser_.Active()) {
    const Ui2SettingsBrowserMode mode = settingsBrowser_.Mode();
    const Ui2SettingsBrowserCommand command =
        settingsBrowser_.Handle(action, pressed);
    if (command.type == Ui2SettingsBrowserCommandType::Back) {
      settingsBrowser_.Close();
      ActivatePage(mode == Ui2SettingsBrowserMode::Theme
                       ? UiApplicationPage::Theme
                       : UiApplicationPage::Font);
      return;
    }
    if (command.type == Ui2SettingsBrowserCommandType::ImportTheme) {
      Config *config = Config::GetInstance();
      bool loaded = false;
      const bool persisted =
          config != nullptr &&
          config->ImportTheme(command.theme.data(), &loaded);
      if (loaded) {
        configSave_.MarkDirty();
        ApplyCurrentTheme();
      }
      if (!persisted) {
        settingsBrowser_.SetError("THEME IMPORT FAILED");
        runtime_.Invalidate();
        return;
      }
      configSave_.MarkSaved();
      settingsBrowser_.Close();
      ActivatePage(UiApplicationPage::Theme);
      return;
    }
    return;
  }
  if (instrumentBrowserActive_) {
    const Ui2InstrumentBrowserCommand command =
        instrumentBrowser_.Handle(action, pressed);
    if (command.type != Ui2InstrumentBrowserCommandType::Import)
      return;

    TrackerSessionState &editor = session_.EditorState();
    const std::uint8_t number =
        static_cast<std::uint8_t>(editor.currentInstrumentID_);
    InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
    PersistencyService *persistence = PersistencyService::GetInstance();
    const Ui2InstrumentImportOutcome result = Ui2InstrumentWorkflow::Import(
        bank, number, command.filename.data(), persistence != nullptr,
        Player::GetInstance()->IsAudioActive(),
        [persistence](const char *filename) {
          return persistence->DetectInstrumentType(filename);
        },
        [persistence, &command](I_Instrument *candidate) {
          return persistence->ImportInstrument(candidate,
                                               command.filename.data()) ==
                 PERSIST_LOADED;
        });
    if (result != Ui2InstrumentImportOutcome::Imported) {
      const char *message = Ui2InstrumentImportFailureText(result);
      instrumentBrowser_.SetError(message);
      Status::Set("%s", message);
      runtime_.Invalidate();
      return;
    }
    instrumentBrowserActive_ = false;
    source_.SetInstrumentBrowserActive(false);
    MarkProjectDirty();
    ActivatePage(UiApplicationPage::Instrument);
    return;
  }
  if (sampleBrowser_.Active()) {
    ExecuteSampleBrowser(sampleBrowser_.Handle(action, pressed));
    return;
  }
  // Project Browser keeps the global transport available. Sample Browser is
  // deliberately excluded above because PLAY there owns press/release sample
  // preview instead of sequencer transport.
  if (action == TrackerAction::Play) {
    if (pressed) {
      Player::GetInstance()->OnStartButton(
          PM_SONG, session_.EditorState().songX_, false,
          static_cast<unsigned char>(session_.EditorState().songX_));
    }
    return;
  }
  const Ui2ProjectBrowserCommand command =
      projectBrowser_.Handle(action, pressed);
  Ui2ProjectLifecycleCommand lifecycleCommand;
  if (command.type == Ui2ProjectBrowserCommandType::Load) {
    lifecycleCommand =
        projectLifecycle_.RequestLoad(command.project.data(), autoSave_.Dirty(),
                                      Player::GetInstance()->IsRunning(),
                                      TrackerAction::Enter);
  } else if (command.type == Ui2ProjectBrowserCommandType::Delete) {
    lifecycleCommand = projectLifecycle_.RequestDelete(
        command.project.data(), session_.ProjectName(),
        Player::GetInstance()->IsRunning(), TrackerAction::Enter);
  }
  ExecuteProjectLifecycle(lifecycleCommand);
}

UiApplicationPage Ui2TrackerApplication::BrowserReturnPage() const {
  if (settingsBrowser_.Mode() == Ui2SettingsBrowserMode::Theme)
    return UiApplicationPage::Theme;
  if (sampleBrowser_.Active())
    return UiApplicationPage::Project;
  return instrumentBrowserActive_ ? UiApplicationPage::Instrument
                                  : UiApplicationPage::Project;
}

void Ui2TrackerApplication::HandleSampleBrowserDialog(TrackerAction action,
                                                       bool pressed) {
  ExecuteSampleBrowser(sampleBrowser_.HandleDialog(action, pressed));
}

void Ui2TrackerApplication::CloseSampleBrowser() {
  if (!sampleBrowser_.Active())
    return;
  Player *player = Player::GetInstance();
  if (player != nullptr && !player->IsRunning() && player->IsPlaying())
    player->StopStreaming();
  sampleBrowser_.Close();
}

void Ui2TrackerApplication::ExecuteSampleBrowser(
    Ui2SampleBrowserCommand command) {
  if (command.type == Ui2SampleBrowserCommandType::Back) {
    CloseSampleBrowser();
    ActivatePage(UiApplicationPage::Project);
    return;
  }
  if (!command.HasValue())
    return;
  Player *player = Player::GetInstance();
  FileSystem *fileSystem = FileSystem::GetInstance();
  SamplePool *pool = SamplePool::GetInstance();

  if (command.type == Ui2SampleBrowserCommandType::PreviewStop) {
    if (player != nullptr && !player->IsRunning() && player->IsPlaying())
      player->StopStreaming();
    return;
  }
  if (command.type == Ui2SampleBrowserCommandType::ModeChanged) {
    if (player != nullptr && !player->IsRunning() && player->IsPlaying())
      player->StopStreaming();
    return;
  }
  if (command.type == Ui2SampleBrowserCommandType::PreviewStart) {
    if (player == nullptr || player->IsRunning() || fileSystem == nullptr ||
        command.filename[0] == '\0') {
      sampleBrowser_.SetError("PREVIEW UNAVAILABLE");
      return;
    }
    WavFile wave;
    const auto opened = wave.Open(command.filename.data());
    if (!opened) {
      sampleBrowser_.SetError("INVALID SAMPLE");
      return;
    }
    wave.Close();
    if (player->IsPlaying())
      player->StopStreaming();
    if (command.singleCycle)
      player->StartLoopingStreaming(command.filename.data());
    else
      player->StartStreaming(command.filename.data());
    sampleBrowser_.ClearError();
    return;
  }

  if (command.type == Ui2SampleBrowserCommandType::AdjustPreviewVolume) {
    Variable *volume =
        session_.ProjectModel().FindVariable(FourCC::VarPreviewVolume);
    if (volume != nullptr) {
      volume->SetInt(std::clamp(volume->GetInt() + command.delta, 0, 99));
      MarkProjectDirty();
    }
    return;
  }

  if (command.type == Ui2SampleBrowserCommandType::Edit) {
    if (player == nullptr || player->IsRunning() || fileSystem == nullptr ||
        command.filename[0] == '\0' ||
        !OpenSampleEditor(command.filename.data(), command.projectSample,
                          UiApplicationPage::Browser)) {
      sampleBrowser_.SetError("INVALID SAMPLE");
      return;
    }
    sampleBrowser_.ClearError();
    return;
  }

  if (command.type == Ui2SampleBrowserCommandType::RequestDelete) {
    if (!command.projectSample || player == nullptr || player->IsRunning() ||
        player->IsPlaying() || pool == nullptr) {
      sampleBrowser_.SetError("DELETE UNAVAILABLE");
      return;
    }
    Project &project = session_.ProjectModel();
    if (project.SampleInUse(
            etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(
                command.filename.data()))) {
      sampleBrowser_.SetError("SAMPLE IN USE");
      return;
    }
    sampleBrowser_.RequestDeleteConfirmation(command.filename.data(),
                                             TrackerAction::Enter);
    return;
  }

  if (command.type == Ui2SampleBrowserCommandType::DeleteConfirmed) {
    if (player == nullptr || player->IsRunning() || player->IsPlaying() ||
        fileSystem == nullptr || pool == nullptr ||
        session_.ProjectModel().SampleInUse(
            etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(
                command.filename.data()))) {
      sampleBrowser_.SetError("DELETE UNAVAILABLE");
      return;
    }
    const Ui2DeleteProjectSampleResult result =
        Ui2DeleteProjectSampleSafely(*fileSystem, *pool,
                                     session_.ProjectName(),
                                     command.filename.data());
    if (result == Ui2DeleteProjectSampleResult::Deleted ||
        result == Ui2DeleteProjectSampleResult::CleanupFailed) {
      sampleBrowser_.RefreshCurrentDirectory();
      if (result == Ui2DeleteProjectSampleResult::CleanupFailed)
        sampleBrowser_.SetError("DELETE CLEANUP FAILED");
      MarkProjectDirty();
    } else if (result == Ui2DeleteProjectSampleResult::UnloadFailed) {
      sampleBrowser_.SetError("DELETE UNSUPPORTED");
    } else if (result == Ui2DeleteProjectSampleResult::RollbackFailed) {
      sampleBrowser_.SetError("DELETE RECOVERY FAILED");
    } else {
      sampleBrowser_.SetError("DELETE FAILED");
    }
    return;
  }

  if (command.type != Ui2SampleBrowserCommandType::Import)
    return;
  if (player == nullptr || player->IsRunning() || player->IsPlaying() ||
      fileSystem == nullptr || pool == nullptr || command.projectSample ||
      command.filename[0] == '\0') {
    sampleBrowser_.SetError("IMPORT UNAVAILABLE");
    return;
  }
  const char *error = nullptr;
  if (!ImportSampleToCurrentInstrument(command.filename.data(), error)) {
    sampleBrowser_.SetError(error);
    return;
  }
  sampleBrowser_.ClearError();
}

bool Ui2TrackerApplication::ImportSampleToCurrentInstrument(
    const char *path, const char *&error) {
  error = "IMPORT FAILED";
  FileSystem *fileSystem = FileSystem::GetInstance();
  SamplePool *pool = SamplePool::GetInstance();
  if (path == nullptr || path[0] == '\0' || fileSystem == nullptr ||
      pool == nullptr) {
    error = "IMPORT UNAVAILABLE";
    return false;
  }
  if (pool->GetNameListSize() >= MAX_SAMPLES) {
    error = "SAMPLE POOL FULL";
    return false;
  }

  // Validate before opening the project destination. ImportSample applies the
  // configured resampler while copying, exactly as the established browser
  // path, and the preflight keeps a failed import from replacing an existing
  // project sample.
  WavFile sourceWave;
  if (!sourceWave.Open(path)) {
    error = "INVALID SAMPLE";
    return false;
  }
  const std::uint32_t sourceBytes = sourceWave.GetDiskSize(-1);
  sourceWave.Close();
  if (sourceBytes == 0U || !pool->CheckSampleFits(sourceBytes)) {
    error = "SAMPLE TOO LARGE";
    return false;
  }

  Ui2ProjectSampleName importedName{};
  Ui2ProjectSamplePath importedPath{};
  if (!Ui2ResolveImportedSampleName(path, importedName) ||
      !Ui2BuildProjectSamplePath(session_.ProjectName(), importedName.data(),
                                 importedPath) ||
      fileSystem->exists(importedPath.data())) {
    error = "SAMPLE ALREADY EXISTS";
    return false;
  }

  const int sampleId = pool->ImportSample(path, session_.ProjectName());
  if (sampleId < 0) {
    // The destination did not exist at preflight, so this can only remove a
    // partial file created by the failed import.
    if (fileSystem->exists(importedPath.data()))
      (void)fileSystem->DeleteFile(importedPath.data());
    return false;
  }

  InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
  const std::uint8_t instrumentNumber = static_cast<std::uint8_t>(
      session_.EditorState().currentInstrumentID_);
  I_Instrument *instrument =
      bank == nullptr ? nullptr : bank->GetInstrument(instrumentNumber);
  if (instrument != nullptr && instrument->GetType() == IT_SAMPLE) {
    auto *sample = static_cast<SampleInstrument *>(instrument);
    sample->AssignSample(sampleId);
    sample->ClearSlices();
  }
  error = nullptr;
  MarkProjectDirty();
  return true;
}

bool Ui2TrackerApplication::OpenSampleEditor(const char *path,
                                             bool projectPool,
                                             UiApplicationPage returnPage) {
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr || path == nullptr || path[0] == '\0')
    return false;
  StopSamplePreview();
  if (sampleEditor_.Active() && !CloseSampleEditor())
    return false;
  if (sampleSlices_.Active())
    sampleSlices_.Close();

  Ui2ProjectSamplePath projectDestination{};
  const char *destination = path;
  if (projectPool) {
    if (!Ui2BuildProjectSamplePath(session_.ProjectName(), path,
                                   projectDestination))
      return false;
    destination = projectDestination.data();
  }
  // Recover an interrupted promotion before the waveform tries to open the
  // authoritative destination. Loading first would make a valid backup
  // unreachable whenever the prior SAVE left the destination absent.
  if (sampleEditorTransaction_.Begin(*fileSystem, destination) !=
      Ui2SampleEditorTransactionResult::Ready)
    return false;

  const Ui2SampleWaveformLoadResult result =
      projectPool
          ? sampleEditor_.OpenProjectPool(*fileSystem, session_.ProjectName(),
                                          path)
          : sampleEditor_.OpenPath(*fileSystem, path, false);
  if (result != Ui2SampleWaveformLoadResult::Loaded) {
    (void)sampleEditorTransaction_.Discard();
    sampleEditorTransaction_.Reset();
    sampleEditor_.Close();
    return false;
  }
  // Expose only the transactional same-name rewrite. Pool-aware rename is not
  // part of the editor contract until references and collisions are atomic.
  sampleEditor_.SetTransactionCapabilities(true);
  sampleReturnPage_ = returnPage;
  if (!ActivatePage(UiApplicationPage::SampleEditor)) {
    (void)CloseSampleEditor();
    return false;
  }
  return true;
}

bool Ui2TrackerApplication::CloseSampleEditor() {
  if (sampleEditorTransaction_.ApplyActive())
    return false;
  StopSamplePreview();
  if (sampleEditorTransaction_.Active() &&
      sampleEditorTransaction_.Discard() !=
          Ui2SampleEditorTransactionResult::Discarded) {
    ShowFeedbackError("SAMPLE DISCARD FAILED");
    return false;
  }
  sampleEditorTransaction_.Reset();
  sampleEditor_.Close();
  return true;
}

bool Ui2TrackerApplication::RecoverSampleEditorDestination() {
  StopSamplePreview();
  const auto failClosed = [this]() {
    // Journal files are intentionally left in place when cleanup/recovery
    // itself fails. A later Begin() can recover the destination without
    // exposing a controller whose waveform path may no longer exist.
    sampleEditorTransaction_.Reset();
    sampleEditor_.Close();
    (void)ActivatePage(sampleReturnPage_);
    ShowFeedbackError("SAMPLE RECOVERY FAILED");
    return false;
  };

  FileSystem *fileSystem = FileSystem::GetInstance();
  std::array<char, PFILENAME_SIZE> destination{};
  const int written =
      std::snprintf(destination.data(), destination.size(), "%s",
                    sampleEditorTransaction_.DestinationPath());
  if (fileSystem == nullptr || written <= 0 ||
      static_cast<std::size_t>(written) >= destination.size())
    return failClosed();

  // Reopen the journal before deleting anything. In the rollback-failure
  // state the backup and working files may be the only recovery evidence.
  if (sampleEditorTransaction_.Begin(*fileSystem, destination.data()) !=
          Ui2SampleEditorTransactionResult::Ready ||
      !sampleEditor_.ReloadPath(*fileSystem, destination.data())) {
    (void)sampleEditorTransaction_.Discard();
    return failClosed();
  }
  sampleEditor_.SetTransactionCapabilities(true);
  return true;
}

bool Ui2TrackerApplication::OpenSampleSlices(const char *path,
                                             UiApplicationPage returnPage) {
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr || path == nullptr || path[0] == '\0')
    return false;
  StopSamplePreview();
  if (sampleEditor_.Active() && !CloseSampleEditor())
    return false;
  if (sampleSlices_.Active())
    sampleSlices_.Close();
  if (sampleSlices_.OpenProjectPool(*fileSystem, session_.ProjectName(), path) !=
      Ui2SampleWaveformLoadResult::Loaded)
    return false;
  sampleReturnPage_ = returnPage;
  SynchronizeSampleSlices();
  ActivatePage(UiApplicationPage::SampleSlices);
  return activePage_ == UiApplicationPage::SampleSlices;
}

void Ui2TrackerApplication::HandleSampleEditor(TrackerAction action,
                                               bool pressed) {
  // Reject before the controller latches PLAY; otherwise a blocked preview
  // would leave its local `playing` visual true until a later release.
  if (action == TrackerAction::Play && pressed) {
    Player *player = Player::GetInstance();
    if (player == nullptr || player->IsRunning())
      return;
  }
  ExecuteSampleEditor(sampleEditor_.Handle(action, pressed));
}

void Ui2TrackerApplication::HandleSampleEditorDialog(TrackerAction action,
                                                     bool pressed) {
  ExecuteSampleEditor(sampleEditor_.HandleDialog(action, pressed));
}

bool Ui2TrackerApplication::ReloadSampleEditorTransactionView() {
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr || !sampleEditor_.Active() ||
      !sampleEditorTransaction_.Active())
    return false;
  const char *const path = sampleEditorTransaction_.HasWorkingCopy()
                               ? sampleEditorTransaction_.WorkingPath()
                               : sampleEditorTransaction_.DestinationPath();
  return sampleEditor_.ReloadPath(*fileSystem, path);
}

void Ui2TrackerApplication::CompleteSampleEditorApply(
    Ui2SampleEditorTransactionResult result) {
  sampleEditor_.FinishApplyProgress();
  runtime_.Invalidate();
  if (result == Ui2SampleEditorTransactionResult::NoChanges)
    return;
  if (result == Ui2SampleEditorTransactionResult::Applied) {
    if (!ReloadSampleEditorTransactionView())
      ShowFeedbackError("SAMPLE RELOAD FAILED");
    return;
  }
  if (result == Ui2SampleEditorTransactionResult::Cancelled)
    return;

  // Apply never changes the authoritative destination or the previous valid
  // working generation. Keep the controller's markers, zoom and viewport
  // exactly intact; reopening recovery here would discard a preserved prior
  // edit, while an unnecessary reload would reset the editor's local state.
  ShowFeedbackError(result == Ui2SampleEditorTransactionResult::RecoveryFailed
                        ? "SAMPLE RECOVERY REQUIRED"
                        : "SAMPLE OPERATION FAILED");
}

void Ui2TrackerApplication::TickSampleEditorApply() {
  if (!sampleEditorTransaction_.ApplyActive())
    return;
  const Ui2SampleEditorTransactionResult result =
      sampleEditorTransaction_.StepApply();
  sampleEditor_.UpdateApplyProgress(sampleEditorTransaction_.ApplyProgress());
  runtime_.Invalidate();
  if (result != Ui2SampleEditorTransactionResult::InProgress)
    CompleteSampleEditorApply(result);
}

void Ui2TrackerApplication::ExecuteSampleEditor(
    Ui2SampleEditorCommand command) {
  if (!command.HasValue())
    return;
  Player *player = Player::GetInstance();
  switch (command.type) {
  case Ui2SampleEditorCommandType::PreviewStart: {
    if (player == nullptr || player->IsRunning() || command.path[0] == '\0')
      return;
    StopSamplePreview();
    const bool singleCycle =
        command.singleCycle &&
        static_cast<std::uint64_t>(sampleWaveform_.FrameCount()) *
                sampleWaveform_.ChannelCount() <=
            Ui2SingleCycleMaximumFrames;
    if (singleCycle)
      player->StartLoopingStreaming(command.path.data());
    else
      player->StartStreaming(command.path.data(),
                             static_cast<int>(command.start));
    // StopSamplePreview() clears any prior controller projection as well as
    // the audio owner. Re-arm the controller that owns this new command.
    sampleEditor_.StartPreview(singleCycle ? 0U : command.start);
    samplePreviewKind_ = SamplePreviewKind::EditorStream;
    samplePreviewStartedMs_ = System::GetInstance()->Millis();
    samplePreviewFrames_ = sampleWaveform_.FrameCount();
    samplePreviewStart_ = singleCycle ? 0U : command.start;
    samplePreviewEnd_ = singleCycle && samplePreviewFrames_ != 0U
                            ? samplePreviewFrames_ - 1U
                            : command.end;
    // AudioFileStreamer intentionally maps one single-cycle buffer to C4
    // (261.63 cycles/s), independent of the WAV header rate.
    samplePreviewRate_ = singleCycle
                             ? static_cast<std::uint32_t>(
                                   (static_cast<std::uint64_t>(
                                        samplePreviewFrames_) *
                                        26163U +
                                    50U) /
                                   100U)
                             : sampleWaveform_.SampleRate();
    samplePreviewSingleCycle_ = singleCycle;
    break;
  }
  case Ui2SampleEditorCommandType::PreviewStop:
    StopSamplePreview();
    break;
  case Ui2SampleEditorCommandType::NavigateBack:
  case Ui2SampleEditorCommandType::RequestDiscard:
    (void)ActivatePage(sampleReturnPage_);
    break;
  case Ui2SampleEditorCommandType::SetStart:
  case Ui2SampleEditorCommandType::SetEnd:
    // START/END are an editor transaction, not live Instrument parameters.
    // They stay local until SAVE/APPLY succeeds, matching the legacy editor.
    break;
  case Ui2SampleEditorCommandType::RequestApplyOperation:
    StopSamplePreview();
    sampleEditor_.RequestApplyConfirmation(
        command.operation, command.start, command.end, TrackerAction::Enter);
    break;
  case Ui2SampleEditorCommandType::ApplyConfirmed: {
    StopSamplePreview();
    const Ui2SampleEditorTransactionResult result =
        command.operation == Ui2SampleEditorOperation::Trim
            ? sampleEditorTransaction_.BeginTrim(command.start, command.end)
            : sampleEditorTransaction_.BeginNormalize();
    if (result == Ui2SampleEditorTransactionResult::InProgress) {
      sampleEditor_.BeginApplyProgress(command.operation, TrackerAction::Enter);
      runtime_.Invalidate();
      break;
    }
    CompleteSampleEditorApply(result);
    break;
  }
  case Ui2SampleEditorCommandType::CancelApply: {
    StopSamplePreview();
    const Ui2SampleEditorTransactionResult result =
        sampleEditorTransaction_.CancelApply();
    CompleteSampleEditorApply(result);
    break;
  }
  case Ui2SampleEditorCommandType::RequestSave:
  case Ui2SampleEditorCommandType::RequestSaveAndLoad: {
    StopSamplePreview();
    std::array<char, PFILENAME_SIZE> destination{};
    const int written =
        std::snprintf(destination.data(), destination.size(), "%s",
                      sampleEditorTransaction_.DestinationPath());
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= destination.size()) {
      ShowFeedbackError("SAMPLE SAVE FAILED");
      break;
    }
    const Ui2SampleEditorTransactionResult result =
        sampleEditorTransaction_.Save();
    if (result != Ui2SampleEditorTransactionResult::Saved &&
        result != Ui2SampleEditorTransactionResult::NoChanges) {
      if (Ui2SampleEditorSaveWorkflow::ResolveFailure(result) ==
          Ui2SampleEditorSaveFailureResolution::ReloadDestination) {
        if (RecoverSampleEditorDestination())
          ShowFeedbackError("SAMPLE SAVE FAILED");
      } else {
        // The transaction result guarantees that the controller's current
        // working path still names a validated generation.
        ShowFeedbackError("SAMPLE SAVE FAILED");
      }
      break;
    }

    const Ui2SampleEditorSaveFollowUp followUp =
        Ui2SampleEditorSaveWorkflow::PrepareFollowUp(
            result,
            command.type == Ui2SampleEditorCommandType::RequestSaveAndLoad,
            sampleReturnPage_ == UiApplicationPage::Browser &&
                sampleBrowser_.Active(),
            [this, &destination]() {
              // Promotion recreates the directory entry. Restore the edited
              // leaf before SAVE&LOAD import or return-page rendering can
              // observe stale FAT indexes and metadata.
              (void)sampleBrowser_.RefreshCurrentDirectoryAndSelect(
                  destination.data());
            });
    if (followUp == Ui2SampleEditorSaveFollowUp::SaveAndLoad) {
      const char *error = nullptr;
      const bool imported =
          ImportSampleToCurrentInstrument(destination.data(), error);
      if (imported)
        (void)sampleBrowser_.Open(session_.ProjectName());
      (void)ActivatePage(imported ? UiApplicationPage::Browser
                                  : sampleReturnPage_);
      if (!imported)
        ShowFeedbackError("SAMPLE SAVED; LOAD FAILED");
      break;
    }

    const bool projectPool = command.projectPool;
    (void)ActivatePage(sampleReturnPage_);
    if (projectPool) {
      System *system = System::GetInstance();
      feedback_.ShowMessage("RELOAD PROJECT TO APPLY",
                            system == nullptr ? 0U : system->Millis());
      runtime_.Invalidate();
    }
    break;
  }
  case Ui2SampleEditorCommandType::None:
    break;
  }
}

void Ui2TrackerApplication::HandleSampleSlices(TrackerAction action,
                                               bool pressed) {
  if (action == TrackerAction::Play && pressed) {
    Player *player = Player::GetInstance();
    SampleInstrument *sample = CurrentSampleInstrument(session_);
    if (player == nullptr || player->IsRunning()) {
      ShowFeedbackMessage("STOP PLAYBACK TO PREVIEW");
      return;
    }
    if (sample == nullptr || sample->GetSampleIndex() < 0) {
      ShowFeedbackError("SAMPLE UNAVAILABLE");
      return;
    }
    if (sample->HasSlicesForPlayback() &&
        !sample->IsSliceDefined(sampleSlices_.SelectedSlice())) {
      ShowFeedbackMessage("SLICE SLOT EMPTY");
      return;
    }
  }
  ExecuteSampleSlices(sampleSlices_.Handle(action, pressed));
}

void Ui2TrackerApplication::HandleSampleSlicesDialog(TrackerAction action,
                                                     bool pressed) {
  ExecuteSampleSlices(sampleSlices_.HandleDialog(action, pressed));
}

void Ui2TrackerApplication::ExecuteSampleSlices(
    Ui2SampleSlicesCommand command) {
  if (!command.HasValue())
    return;
  SampleInstrument *sample = CurrentSampleInstrument(session_);
  Player *player = Player::GetInstance();
  const auto commitSlices = [this](SampleInstrument &instrument) {
    instrument.ClearSlices();
    for (std::uint8_t index = 0U; index < SampleInstrument::MaxSlices;
         ++index) {
      if ((sampleSlices_.DefinedMask() &
           static_cast<std::uint16_t>(1U << index)) != 0U)
        instrument.SetSlicePoint(index, sampleSlices_.SlicePoints()[index]);
    }
    SynchronizeSampleSlices();
    MarkProjectDirty();
  };
  switch (command.type) {
  case Ui2SampleSlicesCommandType::PreviewStart: {
    if (player == nullptr || player->IsRunning() || sample == nullptr ||
        sample->GetSampleIndex() < 0)
      return;
    StopSamplePreview();
    std::uint8_t note = static_cast<std::uint8_t>(
        SampleInstrument::SliceNoteBase + command.slice);
    if (!sample->HasSlicesForPlayback()) {
      if (Variable *root =
              sample->FindVariable(FourCC::SampleInstrumentRootNote))
        note = static_cast<std::uint8_t>(
            std::clamp(root->GetInt(), 0, 127));
    }
    const auto instrument = static_cast<std::uint8_t>(
        std::clamp(session_.EditorState().currentInstrumentID_, 0,
                   MAX_INSTRUMENT_COUNT - 1));
    constexpr std::uint8_t previewChannel = SONG_CHANNEL_COUNT - 1U;
    player->PlayNote(instrument, previewChannel, note, 0x7FU);
    sampleSlices_.StartPreview(command.start);
    samplePreviewKind_ = SamplePreviewKind::SliceNote;
    samplePreviewStartedMs_ = System::GetInstance()->Millis();
    samplePreviewStart_ = command.start;
    samplePreviewEnd_ = command.end;
    samplePreviewFrames_ = sampleWaveform_.FrameCount();
    samplePreviewRate_ = sampleWaveform_.SampleRate();
    if (SamplePool *pool = SamplePool::GetInstance()) {
      if (SoundSource *source = pool->GetSource(sample->GetSampleIndex())) {
        const int rate = source->GetSampleRate(note);
        if (rate > 0)
          samplePreviewRate_ = static_cast<std::uint32_t>(rate);
      }
    }
    samplePreviewInstrument_ = instrument;
    samplePreviewNote_ = note;
    // Slice preview is driven by SampleInstrument's own loop mode. The legacy
    // playhead is a one-pass duration indicator even for short waveforms.
    samplePreviewSingleCycle_ = false;
    break;
  }
  case Ui2SampleSlicesCommandType::PreviewStop:
    StopSamplePreview();
    break;
  case Ui2SampleSlicesCommandType::SetSlicePoint:
  case Ui2SampleSlicesCommandType::AddSlice:
    if (sample != nullptr && command.slice < SampleInstrument::MaxSlices) {
      sample->SetSlicePoint(command.slice, command.value);
      SynchronizeSampleSlices();
      MarkProjectDirty();
    }
    break;
  case Ui2SampleSlicesCommandType::RequestAutoSlice:
  case Ui2SampleSlicesCommandType::ReplaceAutoSlices:
    if (sample == nullptr) {
      ShowFeedbackError("AUTO SLICE UNAVAILABLE");
      break;
    }
    sampleSlices_.ApplyEvenSlices(command.count);
    commitSlices(*sample);
    break;
  case Ui2SampleSlicesCommandType::NavigateBack:
    ActivatePage(sampleReturnPage_);
    break;
  case Ui2SampleSlicesCommandType::DeleteSlice:
    if (sample == nullptr) {
      ShowFeedbackError("DELETE UNAVAILABLE");
      break;
    }
    commitSlices(*sample);
    break;
  case Ui2SampleSlicesCommandType::OperationUnavailable:
    switch (command.failure) {
    case Ui2SampleSlicesFailure::AddLocked:
      ShowFeedbackMessage("SLICE ADD LOCKED");
      break;
    case Ui2SampleSlicesFailure::DeleteLocked:
      ShowFeedbackMessage("SLICE SLOT EMPTY");
      break;
    case Ui2SampleSlicesFailure::MoveLimit:
      ShowFeedbackMessage("SLICE LIMIT");
      break;
    case Ui2SampleSlicesFailure::None:
      break;
    }
    break;
  case Ui2SampleSlicesCommandType::SetAutoSliceCount:
  case Ui2SampleSlicesCommandType::None:
    break;
  }
}

void Ui2TrackerApplication::SynchronizeSampleSlices() {
  std::array<std::uint32_t, Ui2SampleSlicesController::SliceCapacity> points{};
  std::uint16_t definedMask = 0U;
  if (SampleInstrument *sample = CurrentSampleInstrument(session_)) {
    for (std::uint8_t index = 0U; index < SampleInstrument::MaxSlices;
         ++index) {
      points[index] = sample->GetSlicePoint(index);
      if (sample->IsSliceDefined(index))
        definedMask |= static_cast<std::uint16_t>(1U << index);
    }
  }
  sampleSlices_.SynchronizeSlices(points, definedMask);
}

void Ui2TrackerApplication::StopSamplePreview() {
  Player *player = Player::GetInstance();
  if (player != nullptr) {
    if (samplePreviewKind_ == SamplePreviewKind::EditorStream)
      player->StopStreaming();
    else if (samplePreviewKind_ == SamplePreviewKind::SliceNote)
      player->StopNote(samplePreviewInstrument_, SONG_CHANNEL_COUNT - 1U);
  }
  samplePreviewKind_ = SamplePreviewKind::None;
  samplePreviewStartedMs_ = 0U;
  samplePreviewStart_ = 0U;
  samplePreviewEnd_ = 0U;
  samplePreviewFrames_ = 0U;
  samplePreviewRate_ = 0U;
  samplePreviewInstrument_ = 0U;
  samplePreviewNote_ = 0U;
  samplePreviewSingleCycle_ = false;
  sampleEditor_.StopPreview();
  sampleSlices_.StopPreview();
}

void Ui2TrackerApplication::UpdateSamplePreview(std::uint32_t nowMs) {
  if (samplePreviewKind_ == SamplePreviewKind::None ||
      samplePreviewRate_ == 0U || samplePreviewFrames_ == 0U)
    return;
  // A non-looping editor stream can reach EOF while PLAY is still held.
  // Legacy SampleEditorView observed Player::IsPlaying() and cleared its
  // visual state at that point; keep UI2's power state synchronized too.
  if (samplePreviewKind_ == SamplePreviewKind::EditorStream) {
    Player *player = Player::GetInstance();
    if (player == nullptr || !player->IsPlaying()) {
      StopSamplePreview();
      return;
    }
  }
  const std::uint64_t elapsed = nowMs - samplePreviewStartedMs_;
  const std::uint64_t advanced = elapsed * samplePreviewRate_ / 1000U;
  const std::uint32_t maximum = samplePreviewFrames_ - 1U;
  const std::uint32_t start = std::min(samplePreviewStart_, maximum);
  const std::uint32_t end =
      std::clamp(samplePreviewEnd_, start, maximum);
  const std::uint64_t span = static_cast<std::uint64_t>(end - start) + 1U;
  bool visible = true;
  std::uint32_t playhead = start;
  if (samplePreviewSingleCycle_ && span != 0U) {
    playhead = static_cast<std::uint32_t>(start + advanced % span);
  } else if (advanced >= span) {
    playhead = end;
    visible = false;
  } else {
    playhead = static_cast<std::uint32_t>(start + advanced);
  }
  if (samplePreviewKind_ == SamplePreviewKind::EditorStream)
    sampleEditor_.SetPreviewPlayhead(playhead, visible);
  else
    sampleSlices_.SetPreviewPlayhead(playhead, visible);
}

void Ui2TrackerApplication::HandleProjectLifecycle(TrackerAction action,
                                                   bool pressed) {
  ExecuteProjectLifecycle(projectLifecycle_.Handle(action, pressed));
}

void Ui2TrackerApplication::HandleGroove(TrackerAction action, bool pressed) {
  ExecuteGroove(groove_.Handle(action, pressed));
}

void Ui2TrackerApplication::ShowTrackerClipboardNotice(
    const Ui2TrackerCommandBatch<> &batch) {
  System *system = System::GetInstance();
  const std::uint32_t nowMs = system == nullptr ? 0U : system->Millis();
  for (std::uint8_t index = 0U; index < batch.count; ++index) {
    const Ui2TrackerCommand &command = batch.commands[index];
    if (command.type == Ui2TrackerCommandType::CopySelection ||
        command.type == Ui2TrackerCommandType::CutSelection) {
      const Ui2TrackerClipboardState clipboard =
          modelPort_.ClipboardState(command.sourcePage);
      if (clipboard.ready)
        clipboardNotice_.ShowCopied(clipboard.width, clipboard.height, nowMs);
    } else if (command.type == Ui2TrackerCommandType::PasteSelection &&
               modelPort_.LastPasteAccepted()) {
      const Ui2TrackerClipboardState clipboard =
          modelPort_.ClipboardState(command.sourcePage);
      if (clipboard.ready)
        clipboardNotice_.ShowPasted(clipboard.width, clipboard.height, nowMs);
    } else if (command.type == Ui2TrackerCommandType::PasteSelection) {
      feedback_.ShowError("CLIPBOARD INCOMPATIBLE", nowMs);
      runtime_.Invalidate();
    }
  }
}

void Ui2TrackerApplication::HandleDevice(TrackerAction action, bool pressed) {
  ExecuteDevice(device_.Handle(action, pressed));
  if (Ui2IsPlainPlay(action, pressed, device_.HeldMask()))
    StartSongTransport(session_);
  if (pressed && action == TrackerAction::Down &&
      (device_.HeldMask() & TrackerActionBit(TrackerAction::Shift)) != 0U)
    ActivatePage(UiApplicationPage::Project);
}

void Ui2TrackerApplication::ExecuteDevice(Ui2DeviceCommand command) {
  switch (command.type) {
  case Ui2DeviceCommandType::BrowseTheme:
    ActivatePage(UiApplicationPage::Theme);
    break;
  case Ui2DeviceCommandType::BrowseFont:
    ActivatePage(UiApplicationPage::Font);
    break;
  case Ui2DeviceCommandType::SetSelector: {
    Config *config = Config::GetInstance();
    FourCC::enum_type key = FourCC::Default;
    int storedValue = command.value;
    switch (command.field) {
    case Ui2DeviceField::MidiDevice:
      key = FourCC::VarMidiDevice;
      break;
    case Ui2DeviceField::MidiSync:
      key = FourCC::VarMidiSync;
      break;
    case Ui2DeviceField::Resampler:
      key = FourCC::VarImportResampler;
      break;
    case Ui2DeviceField::LineOut:
      key = FourCC::VarLineOut;
      break;
    case Ui2DeviceField::Volume:
      key = FourCC::VarOutputVolume;
      break;
    case Ui2DeviceField::Brightness:
      key = FourCC::VarBacklightLevel;
      storedValue = Ui2BrightnessRawFromPercent(command.value);
      break;
    case Ui2DeviceField::Theme:
    case Ui2DeviceField::Font:
    case Ui2DeviceField::UpdateFirmware:
    case Ui2DeviceField::Count:
      break;
    }
    if (key == FourCC::Default)
      break;
    if (Variable *value = config->FindVariable(key))
      value->SetInt(storedValue);
    if (command.field == Ui2DeviceField::Brightness)
      System::GetInstance()->SetDisplayBrightness(
          static_cast<unsigned char>(storedValue));
    else if (command.field == Ui2DeviceField::Volume)
      Audio::GetInstance()->SetMixerVolume(command.value);
    else if (command.field == Ui2DeviceField::LineOut)
      Audio::GetInstance()->SetAudioLevel(command.value);
    configSave_.MarkDirty();
    break;
  }
  case Ui2DeviceCommandType::UpdateFirmware:
    deviceLifecycle_.RequestUpdateFirmware(
        Player::GetInstance()->IsRunning(), TrackerAction::Enter);
    break;
  case Ui2DeviceCommandType::None:
    break;
  }
}

void Ui2TrackerApplication::HandleDeviceLifecycle(TrackerAction action,
                                                  bool pressed) {
  const Ui2DeviceLifecycleCommand command =
      deviceLifecycle_.Handle(action, pressed);
  if (command.HasValue())
    (void)Ui2DeviceLifecycleService::FromSystem(System::GetInstance())
        .Execute(command);
}

void Ui2TrackerApplication::HandleFont(TrackerAction action, bool pressed) {
  const Ui2FontCommand command = font_.Handle(action, pressed);
  if (Ui2IsPlainPlay(action, pressed, font_.HeldMask()))
    StartSongTransport(session_);
  if (pressed && action == TrackerAction::Left &&
      (font_.HeldMask() & TrackerActionBit(TrackerAction::Shift)) != 0U) {
    ActivatePage(UiApplicationPage::Device);
    return;
  }
  Config *config = Config::GetInstance();
  Variable *configured = nullptr;
  if (config != nullptr) {
    if (command.type == Ui2FontCommandType::SetTextCase)
      configured = config->FindVariable(FourCC::VarUITextCase);
    else if (command.type == Ui2FontCommandType::RestoreDefault)
      configured = config->FindVariable(FourCC::VarUIFont);
  }
  switch (Ui2ExecuteFontCommand(command, configured)) {
  case Ui2FontWorkflowResult::TextCaseChanged:
    // CASE may receive held-direction repeats. Coalesce those writes with
    // Device/Theme settings and persist once at the page boundary instead of
    // synchronously rewriting config for every repeat pulse.
    configSave_.MarkDirty();
    break;
  case Ui2FontWorkflowResult::BrowserUnavailable:
    font_.SetFeedback(Ui2FontFeedback::BrowserUnavailable);
    Status::Set("FONT BROWSER UNAVAILABLE");
    runtime_.Invalidate();
    break;
  case Ui2FontWorkflowResult::ConfigUnavailable:
    font_.SetFeedback(Ui2FontFeedback::ConfigUnavailable);
    Status::Set("FONT CONFIG UNAVAILABLE");
    runtime_.Invalidate();
    break;
  case Ui2FontWorkflowResult::DefaultRestored:
    configSave_.MarkDirty();
    if (FlushConfig()) {
      font_.SetFeedback(Ui2FontFeedback::DefaultRestored);
      Status::Set("DEFAULT FONT RESTORED");
    } else {
      font_.SetFeedback(Ui2FontFeedback::SaveFailed);
      Status::Set("FONT SAVE FAILED");
    }
    runtime_.Invalidate();
    break;
  case Ui2FontWorkflowResult::None:
    break;
  }
}

void Ui2TrackerApplication::HandleRename(TrackerAction action, bool pressed) {
  const Ui2RenameCommand command = rename_.Handle(action, pressed);
  if (command == Ui2RenameCommand::Randomize) {
    rename_.Randomize(System::GetInstance()->GetRandomNumber());
  } else if (command == Ui2RenameCommand::Save) {
    if (renameTarget_ == RenameTarget::Instrument) {
      InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
      I_Instrument *instrument =
          bank == nullptr ? nullptr : bank->GetInstrument(renameInstrumentNumber_);
      if (instrument != nullptr) {
        instrument->SetName(rename_.Value());
        MarkProjectDirty();
      }
    } else if (renameTarget_ == RenameTarget::Project) {
      const bool saveAfterRename = deferredProjectSave_.CompleteRename();
      projectSaveAsPending_ =
          std::strcmp(savedProjectName_.data(), rename_.Value()) != 0;
      session_.ProjectModel().SetProjectName(rename_.Value());
      autoSave_.SetSaveAsPending(projectSaveAsPending_);
      MarkProjectDirty();
      renameTarget_ = RenameTarget::None;
      if (saveAfterRename)
        SaveCurrentProject();
      return;
    } else if (renameTarget_ == RenameTarget::Theme ||
               renameTarget_ == RenameTarget::NewTheme) {
      CommitThemeName(rename_.Value(),
                      renameTarget_ == RenameTarget::NewTheme);
    }
    renameTarget_ = RenameTarget::None;
  } else if (command == Ui2RenameCommand::Cancel) {
    deferredProjectSave_.Cancel();
    renameTarget_ = RenameTarget::None;
  }
}

void Ui2TrackerApplication::CommitThemeName(const char *name,
                                            bool resetColors) {
  Config *config = Config::GetInstance();
  if (config == nullptr || !Config::IsValidThemeName(name))
    return;
  if (resetColors)
    config->ResetSemanticThemeColors();
  if (Variable *themeName = config->FindVariable(FourCC::VarThemeName))
    themeName->SetString(name);
  configSave_.MarkDirty();
  (void)FlushConfig();
  ApplyCurrentTheme();
}

void Ui2TrackerApplication::HandleMixer(TrackerAction action, bool pressed) {
  const Ui2MixerCommand command = mixer_.Handle(action, pressed);
  if (command.type == Ui2MixerCommandType::ReturnToSong) {
    ActivatePage(UiApplicationPage::Song);
    return;
  }
  if (command.type == Ui2MixerCommandType::ToggleMute ||
      command.type == Ui2MixerCommandType::ToggleSolo ||
      command.type == Ui2MixerCommandType::UnmuteAll) {
    // Master is an output gain only. It is not a ninth sequencer channel and
    // must never alias mute/solo operations onto T1.
    if (command.channel >= SONG_CHANNEL_COUNT &&
        command.type != Ui2MixerCommandType::UnmuteAll)
      return;
    // Mixer and tracker grids share the model port's single saved solo mask.
    // Toggling solo in one view must therefore restore correctly from the
    // other instead of maintaining two competing pieces of state.
    Ui2TrackerCommand trackerCommand{};
    trackerCommand.sourcePage = Ui2TrackerPage::Mixer;
    trackerCommand.track =
        command.channel < SONG_CHANNEL_COUNT ? command.channel : 0U;
    trackerCommand.type = command.type == Ui2MixerCommandType::ToggleMute
                              ? Ui2TrackerCommandType::ToggleMute
                          : command.type == Ui2MixerCommandType::ToggleSolo
                              ? Ui2TrackerCommandType::ToggleSolo
                              : Ui2TrackerCommandType::UnmuteAll;
    modelPort_.ApplyGridCommand(trackerCommand);
    return;
  }
  if (command.type == Ui2MixerCommandType::SelectChannel) {
    if (command.channel < SONG_CHANNEL_COUNT)
      session_.EditorState().songX_ = command.channel;
    return;
  }
  if (command.type == Ui2MixerCommandType::StartPlayback) {
    StartSongTransport(session_);
    return;
  }
  if (command.type != Ui2MixerCommandType::AdjustVolume)
    return;

  Project &project = session_.ProjectModel();
  static constexpr FourCC channelVariables[SONG_CHANNEL_COUNT] = {
      FourCC::VarChannel1Volume, FourCC::VarChannel2Volume,
      FourCC::VarChannel3Volume, FourCC::VarChannel4Volume,
      FourCC::VarChannel5Volume, FourCC::VarChannel6Volume,
      FourCC::VarChannel7Volume, FourCC::VarChannel8Volume};
  const bool master = command.channel == SONG_CHANNEL_COUNT;
  Variable *variable = project.FindVariable(
      master ? FourCC::VarMasterVolume : channelVariables[command.channel]);
  if (variable != nullptr) {
    variable->SetInt(std::clamp(variable->GetInt() + command.delta, 0, 99));
    MarkProjectDirty();
  }
}

void Ui2TrackerApplication::HandleInstrument(TrackerAction action,
                                             bool pressed) {
  ConfigureInstrumentSubfields(session_, instrument_);
  ExecuteInstrument(instrument_.Handle(action, pressed));
}

void Ui2TrackerApplication::ExecuteInstrument(Ui2InstrumentCommand command) {
  if (!command.HasValue())
    return;
  TrackerSessionState &editor = session_.EditorState();
  InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
  const std::uint8_t number =
      static_cast<std::uint8_t>(editor.currentInstrumentID_);
  I_Instrument *instrument =
      bank == nullptr ? nullptr : bank->GetInstrument(number);

  if (command.type == Ui2InstrumentCommandType::LoadInstrument) {
    const bool refreshed = instrumentBrowser_.Refresh();
    if (!refreshed) {
      instrumentBrowser_.SetError("INSTRUMENT BROWSER FAILED");
      Status::Set("INSTRUMENT BROWSER FAILED");
    }
    CloseSampleBrowser();
    settingsBrowser_.Close();
    instrumentBrowserActive_ = true;
    source_.SetInstrumentBrowserActive(true);
    ActivatePage(UiApplicationPage::Browser);
    return;
  }
  if (command.type == Ui2InstrumentCommandType::SaveInstrument) {
    SaveCurrentInstrument();
    return;
  }
  if (command.type == Ui2InstrumentCommandType::RenameInstrument) {
    if (!Ui2InstrumentWorkflow::CanRename(instrument)) {
      constexpr const char *message = "NO INSTRUMENT TO RENAME";
      Status::Set("%s", message);
      ShowFeedbackError(message);
      return;
    }
    const auto name = instrument->GetUserSetName();
    renameTarget_ = RenameTarget::Instrument;
    renameInstrumentNumber_ = number;
    rename_.Begin(name.c_str(), MAX_INSTRUMENT_NAME_LENGTH, nullptr,
                  TrackerAction::Enter);
    return;
  }
  if (command.type == Ui2InstrumentCommandType::ActivateField) {
    if (instrument != nullptr &&
        command.cursor.kind == Ui2InstrumentCursorKind::Field) {
      const Ui2InstrumentParameterDescriptor descriptor =
          ActiveInstrumentParameter(session_, command.cursor);
      Variable *value = descriptor.primary == FourCC::Default
                            ? nullptr
                            : instrument->FindVariable(descriptor.primary);
      const bool tableField =
          descriptor.primary == FourCC::SampleInstrumentTable ||
          descriptor.primary == FourCC::MidiInstrumentTable;
      if (tableField) {
        TableHolder *tables = TableHolder::GetInstance();
        if (value != nullptr && tables != nullptr &&
            Ui2AllocateInstrumentTable(descriptor, *value, *tables)) {
          MarkProjectDirty();
          return;
        }
        Status::Set("NO FREE TABLE");
        ShowFeedbackError("NO FREE TABLE");
        return;
      }

      if (instrument->GetType() != IT_SAMPLE || command.cursor.index > 1U)
        return;
      auto *sample = static_cast<SampleInstrument *>(instrument);
      const auto filename = sample->GetSampleFileName();
      const Ui2InstrumentSampleOpenOutcome openOutcome =
          Ui2InstrumentSampleOpenOutcomeFor(
              sample->GetSampleIndex(), !filename.empty(),
              Player::GetInstance()->IsRunning());
      if (openOutcome != Ui2InstrumentSampleOpenOutcome::Available) {
        const char *message = Ui2InstrumentSampleOpenFailureText(openOutcome);
        Status::Set("%s", message);
        ShowFeedbackError(message);
        return;
      }
      if (command.cursor.index == 0U)
        (void)OpenSampleEditor(filename.c_str(), true,
                               UiApplicationPage::Instrument);
      else if (command.cursor.index == 1U)
        (void)OpenSampleSlices(filename.c_str(),
                               UiApplicationPage::Instrument);
    }
    return;
  }

  if (command.type == Ui2InstrumentCommandType::SelectNumber) {
    editor.currentInstrumentID_ = command.value;
    return;
  }
  if (command.type == Ui2InstrumentCommandType::SelectTrack) {
    editor.songX_ = command.value;
    return;
  }
  if (command.type == Ui2InstrumentCommandType::StartPlayback) {
    Player::GetInstance()->OnStartButton(PM_PHRASE, editor.songX_, false,
                                         editor.chainRow_);
    return;
  }
  if (command.type == Ui2InstrumentCommandType::SetType) {
    const InstrumentType requested = static_cast<InstrumentType>(
        std::clamp<int>(command.value, IT_NONE, IT_LAST - 1));
    const InstrumentType current =
        instrument == nullptr ? IT_NONE : instrument->GetType();
    const Ui2InstrumentLifecycleCommand lifecycleCommand =
        instrumentLifecycle_.RequestTypeChange(
            requested, current,
            Ui2InstrumentNeedsTypeChangeConfirmation(instrument),
            Player::GetInstance()->IsAudioActive(),
            InstrumentTypeChangeTrigger(command.direction));
    ExecuteInstrumentLifecycle(lifecycleCommand);
    return;
  }
  if (command.type != Ui2InstrumentCommandType::AdjustField ||
      instrument == nullptr)
    return;

  const Ui2InstrumentParameterDescriptor descriptor =
      ActiveInstrumentParameter(session_, command.cursor);
  if (!descriptor.Valid() || !descriptor.editable ||
      descriptor.primary == FourCC::Default)
    return;
  Variable *value = instrument->FindVariable(descriptor.primary);
  if (value == nullptr)
    return;
  const int previous = value->GetInt();
  const int adjusted = command.subfieldMode == Ui2InstrumentSubfieldMode::None
                           ? Ui2AdjustInstrumentParameter(
                                 descriptor, previous, command.direction)
                           : Ui2AdjustInstrumentSubfieldParameter(
                                 descriptor, previous, command.subfieldMode,
                                 command.subfield, command.direction);
  if (adjusted == previous)
    return;
  value->SetInt(adjusted);
  if (descriptor.primary == FourCC::MidiInstrumentProgram) {
    // Match InstrumentView::Update: changing PROGRAM during playback emits a
    // MIDI Program Change immediately, including the legacy OFF sentinel.
    auto *midi = static_cast<MidiInstrument *>(instrument);
    Variable *channel = midi->FindVariable(FourCC::MidiInstrumentChannel);
    if (channel != nullptr)
      (void)Ui2ApplyInstrumentSideEffect(
          descriptor, Player::GetInstance()->IsRunning(), true, *midi,
          channel->GetInt(), adjusted);
  }
  MarkProjectDirty();
}

void Ui2TrackerApplication::HandleInstrumentLifecycle(TrackerAction action,
                                                       bool pressed) {
  ExecuteInstrumentLifecycle(instrumentLifecycle_.Handle(action, pressed));
}

void Ui2TrackerApplication::ExecuteInstrumentLifecycle(
    Ui2InstrumentLifecycleCommand command) {
  if (!command.HasValue())
    return;
  if (command.type == Ui2InstrumentLifecycleCommandType::OverwriteExport) {
    SaveCurrentInstrument(true);
    return;
  }
  if (command.type != Ui2InstrumentLifecycleCommandType::ApplyType)
    return;
  InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
  const auto slot = static_cast<unsigned short>(
      session_.EditorState().currentInstrumentID_);
  const Ui2InstrumentTypeOutcome result = Ui2InstrumentWorkflow::ChangeType(
      bank, slot, command.instrumentType,
      Player::GetInstance()->IsAudioActive());
  if (result == Ui2InstrumentTypeOutcome::PlayingBlocked) {
    I_Instrument *current = bank == nullptr ? nullptr : bank->GetInstrument(slot);
    (void)instrumentLifecycle_.RequestTypeChange(
        command.instrumentType,
        current == nullptr ? IT_NONE : current->GetType(), false, true);
    return;
  }
  if (result != Ui2InstrumentTypeOutcome::Changed) {
    // The atomic transaction deliberately retains the current slot. Surface
    // the real failure through UI2's non-blocking feedback layer instead of
    // leaving the selector apparently unresponsive.
    const char *message = Ui2InstrumentTypeFailureText(result);
    if (message[0] != '\0') {
      Status::Set("%s", message);
      ShowFeedbackError(message);
    }
    return;
  }
  MarkProjectDirty();
}

void Ui2TrackerApplication::SaveCurrentInstrument(bool overwrite) {
  InstrumentBank *bank = session_.ProjectModel().GetInstrumentBank();
  const auto slot = static_cast<unsigned short>(
      session_.EditorState().currentInstrumentID_);
  I_Instrument *instrument =
      bank == nullptr ? nullptr : bank->GetInstrument(slot);
  PersistencyService *persistence = PersistencyService::GetInstance();
  const Ui2InstrumentExportOutcome result = Ui2InstrumentWorkflow::Export(
      instrument, overwrite,
      [persistence](I_Instrument *candidate, const char *name,
                    bool replaceExisting) {
        if (persistence == nullptr)
          return Ui2InstrumentStorageResult::Failed;
        const PersistencyResult stored = persistence->ExportInstrument(
            candidate, etl::string<MAX_INSTRUMENT_NAME_LENGTH>(name),
            replaceExisting);
        if (stored == PERSIST_SAVED)
          return Ui2InstrumentStorageResult::Saved;
        if (stored == PERSIST_EXISTS)
          return Ui2InstrumentStorageResult::Exists;
        return Ui2InstrumentStorageResult::Failed;
      });
  const Ui2InstrumentExportFeedback presentation =
      Ui2InstrumentExportFeedbackFor(result);
  if (presentation.text[0] != '\0') {
    Status::Set("%s", presentation.text);
    System *system = System::GetInstance();
    const std::uint32_t nowMs = system == nullptr ? 0U : system->Millis();
    if (presentation.error)
      feedback_.ShowError(presentation.text, nowMs);
    else
      feedback_.ShowMessage(presentation.text, nowMs);
  }
  if (result == Ui2InstrumentExportOutcome::Exists) {
    Status::Set("INSTRUMENT FILE EXISTS");
    instrumentLifecycle_.RequestExportOverwrite(TrackerAction::Enter);
  }
  runtime_.Invalidate();
}

void Ui2TrackerApplication::HandleRecord(TrackerAction action, bool pressed) {
  ExecuteRecord(record_.Handle(action, pressed));
}

void Ui2TrackerApplication::ConfigureRecordController() {
  record_.SetAvailable(IsRecordingAvailable());
}

void Ui2TrackerApplication::ExecuteRecord(Ui2RecordCommand command) {
  if (!command.HasValue())
    return;
  Config *config = Config::GetInstance();
  FourCC::enum_type key = FourCC::Default;
  if (command.type == Ui2RecordCommandType::SetSource)
    key = FourCC::VarRecordSource;
  else if (command.type == Ui2RecordCommandType::ToggleRecording) {
    if (IsSavingRecording()) {
      ShowFeedbackMessage("RECORDING IS SAVING");
      return;
    }
    if (IsRecordingActive()) {
      RequestStopRecording();
      return;
    }
    constexpr const char *recordingPath =
        RECORDINGS_DIR "/" RECORDING_FILENAME;
    if (!StartRecording(recordingPath, 0U, 0U)) {
      ShowFeedbackError("RECORDING START FAILED");
      StartMonitoring();
      return;
    }
    recordSessionPending_ = true;
    runtime_.Invalidate();
    return;
  }
  if (key == FourCC::Default)
    return;
  Variable *value =
      config == nullptr ? nullptr : config->FindVariable(FourCC(key));
  if (value == nullptr)
    return;
  value->SetInt(command.value);
  if (command.type == Ui2RecordCommandType::SetSource)
    SetInputSource(static_cast<RecordSource>(command.value));
  configSave_.MarkDirty();
}

void Ui2TrackerApplication::TickRecordLifecycle() {
  if (!recordSessionPending_ || IsRecordingActive() || IsSavingRecording())
    return;
  recordSessionPending_ = false;
  if (!DidLastRecordingCaptureAudio()) {
    ShowFeedbackError("RECORDING SAVE FAILED");
    if (activePage_ == UiApplicationPage::Record)
      StartMonitoring();
    return;
  }
  constexpr const char *recordingPath =
      RECORDINGS_DIR "/" RECORDING_FILENAME;
  if (!OpenSampleEditor(recordingPath, false, UiApplicationPage::Record)) {
    ShowFeedbackError("RECORDING OPEN FAILED");
    if (activePage_ == UiApplicationPage::Record)
      StartMonitoring();
  }
}

void Ui2TrackerApplication::HandleTheme(TrackerAction action, bool pressed) {
  const Ui2ThemeCommand command = theme_.Handle(action, pressed);
  if (Ui2IsPlainPlay(action, pressed, theme_.HeldMask()))
    StartSongTransport(session_);
  if (pressed && action == TrackerAction::Left &&
      (theme_.HeldMask() & TrackerActionBit(TrackerAction::Shift)) != 0U) {
    ActivatePage(UiApplicationPage::Device);
    return;
  }
  ExecuteTheme(command);
}

void Ui2TrackerApplication::ExecuteTheme(Ui2ThemeCommand command) {
  Config *config = Config::GetInstance();
  switch (command.type) {
  case Ui2ThemeCommandType::NewTheme:
    renameTarget_ = RenameTarget::NewTheme;
    rename_.Begin("New Theme", MAX_THEME_NAME_LENGTH,
                  &Config::IsValidThemeName, TrackerAction::Enter);
    break;
  case Ui2ThemeCommandType::LoadTheme: {
    std::array<char, MAX_THEME_NAME_LENGTH + 1U> currentName{};
    if (config != nullptr) {
      if (Variable *name = config->FindVariable(FourCC::VarThemeName))
        std::snprintf(currentName.data(), currentName.size(), "%s",
                      name->GetString().c_str());
    }
    instrumentBrowserActive_ = false;
    source_.SetInstrumentBrowserActive(false);
    settingsBrowser_.OpenTheme(currentName.data());
    ActivatePage(UiApplicationPage::Browser);
    break;
  }
  case Ui2ThemeCommandType::SaveTheme:
    if (config != nullptr) {
      Variable *name = config->FindVariable(FourCC::VarThemeName);
      if (name == nullptr || name->GetString().length() == 0U)
        break;
      std::array<char, MAX_THEME_NAME_LENGTH + 16U> path{};
      std::snprintf(path.data(), path.size(), "%s/%s%s", THEMES_DIR,
                    name->GetString().c_str(), THEME_FILE_EXTENSION);
      FileSystem *fileSystem = FileSystem::GetInstance();
      if (fileSystem == nullptr) {
        projectLifecycle_.ReportFailure(Ui2ProjectLifecycleFailure::SaveTheme);
      } else if (fileSystem->exists(path.data())) {
        // Match the legacy flow: an existing theme is never silently replaced.
        // The shared conservative message dialog defaults to NO.
        projectLifecycle_.RequestThemeOverwrite(name->GetString().c_str(),
                                                TrackerAction::Enter);
      } else if (!config->ExportTheme(name->GetString().c_str(), false)) {
        projectLifecycle_.ReportFailure(Ui2ProjectLifecycleFailure::SaveTheme);
      } else {
        configSave_.MarkDirty();
        (void)FlushConfig();
      }
    }
    break;
  case Ui2ThemeCommandType::RenameTheme:
    if (config != nullptr) {
      Variable *name = config->FindVariable(FourCC::VarThemeName);
      renameTarget_ = RenameTarget::Theme;
      rename_.Begin(name == nullptr ? "" : name->GetString().c_str(),
                    MAX_THEME_NAME_LENGTH, &Config::IsValidThemeName,
                    TrackerAction::Enter);
    }
    break;
  case Ui2ThemeCommandType::AdjustColor:
  case Ui2ThemeCommandType::ResetColorComponent:
    if (config != nullptr) {
      const Ui2ThemeColorEditResult edit =
          Ui2ThemeWorkflow::Execute(command, config->GetSemanticThemeColors(),
                                    Config::DefaultSemanticThemeColors());
      if (!edit.changed)
        break;
      config->SetSemanticThemeColor(edit.color, edit.packedColor);
      // The next Theme frame reads the updated Config and rebuilds the
      // palette once. Applying here as well would invalidate that frame and
      // make every key-repeat rebuild all derived colors twice.
      runtime_.Invalidate();
      configSave_.MarkDirty();
    }
    break;
  case Ui2ThemeCommandType::None:
    break;
  }
}

void Ui2TrackerApplication::ApplyCurrentTheme() {
  Config *config = Config::GetInstance();
  if (config != nullptr)
    runtime_.ApplyThemeColors(config->GetSemanticThemeColors());
}

void Ui2TrackerApplication::SaveCurrentProject(bool overwrite) {
  if (pendingSave_ != PendingSaveKind::None)
    return;
  autoSave_.SetPersistBusy(true);
  pendingSave_ = PendingSaveKind::Project;
  pendingSaveOverwrite_ = overwrite;
  persistenceStatus_.BeginSaving();
}

void Ui2TrackerApplication::ExecutePendingSave(std::uint32_t nowMs) {
  const PendingSaveKind kind = pendingSave_;
  const bool overwrite = pendingSaveOverwrite_;
  pendingSave_ = PendingSaveKind::None;
  pendingSaveOverwrite_ = false;

  if (kind == PendingSaveKind::AutoSave) {
    const AutoSaveCoordinator::Conditions conditions = AutoSaveConditions();
    if (!conditions.projectLoaded || conditions.playerRunning ||
        conditions.recordingActive || !conditions.operationAllowsSave) {
      autoSave_.CompleteAutoSave(nowMs, false);
      persistenceStatus_.FinishSaving(nowMs);
      return;
    }
    const bool saved =
        session_.AutoSave(conditions.operationAllowsSave,
                          conditions.recordingActive);
    autoSave_.CompleteAutoSave(nowMs, saved);
    persistenceStatus_.FinishSaving(nowMs);
    if (!saved)
      projectLifecycle_.ReportFailure(Ui2ProjectLifecycleFailure::SaveProject);
    return;
  }
  if (kind != PendingSaveKind::Project) {
    persistenceStatus_.FinishSaving(nowMs);
    return;
  }

  const TrackerApplicationSession::SaveResult result = session_.SaveProject(
      savedProjectName_.data(), projectSaveAsPending_, overwrite);
  autoSave_.SetPersistBusy(false);
  persistenceStatus_.FinishSaving(nowMs);
  if (result == TrackerApplicationSession::SaveResult::Exists) {
    // Saving is deferred until the saving indicator has been presented. A
    // fast tap may therefore release ENTER before this dialog opens; only arm
    // the release gate while the opener is still physically held.
    const TrackerAction trigger =
        (physicalHeldMask_ & TrackerActionBit(TrackerAction::Enter)) != 0U
            ? TrackerAction::Enter
            : TrackerAction::Count;
    projectLifecycle_.RequestOverwrite(session_.ProjectName(), trigger);
    return;
  }
  if (result != TrackerApplicationSession::SaveResult::Saved) {
    projectLifecycle_.ReportFailure(Ui2ProjectLifecycleFailure::SaveProject);
    return;
  }
  std::snprintf(savedProjectName_.data(), savedProjectName_.size(), "%s",
                session_.ProjectName());
  projectSaveAsPending_ = false;
  autoSave_.SetSaveAsPending(false);
  autoSave_.OnProjectSaved(nowMs);
}

void Ui2TrackerApplication::ExecuteProjectLifecycle(
    Ui2ProjectLifecycleCommand command) {
  if (!command.HasValue())
    return;
  if (command.type == Ui2ProjectLifecycleCommandType::OverwriteTheme) {
    Config *config = Config::GetInstance();
    if (config == nullptr || command.project[0] == '\0' ||
        !config->ExportTheme(command.project.data(), true)) {
      projectLifecycle_.ReportFailure(Ui2ProjectLifecycleFailure::SaveTheme);
      return;
    }
    configSave_.MarkDirty();
    (void)FlushConfig();
    return;
  }
  if (command.type == Ui2ProjectLifecycleCommandType::OverwriteProject) {
    SaveCurrentProject(true);
    return;
  }
  if (command.type == Ui2ProjectLifecycleCommandType::PurgeUnusedSamples ||
      command.type ==
          Ui2ProjectLifecycleCommandType::PurgeUnusedInstruments) {
    // Recheck at execution time as well as when opening the confirmation. A
    // transport may start outside this modal's input path, and neither sample
    // files nor live instrument slots may be released while Player owns them.
    if (Player::GetInstance()->IsAudioActive()) {
      if (command.type ==
          Ui2ProjectLifecycleCommandType::PurgeUnusedSamples)
        projectLifecycle_.RequestPurgeUnusedSamples(true, TrackerAction::Enter);
      else
        projectLifecycle_.RequestPurgeUnusedInstruments(true,
                                                        TrackerAction::Enter);
      return;
    }
    if (command.type == Ui2ProjectLifecycleCommandType::PurgeUnusedSamples)
      session_.ProjectModel().PurgeSamples();
    else
      session_.ProjectModel().PurgeInstruments();
    MarkProjectDirty();
    return;
  }

  const std::uint32_t nowMs = System::GetInstance()->Millis();
  autoSave_.SetPersistBusy(true);
  bool succeeded = false;
  switch (command.type) {
  case Ui2ProjectLifecycleCommandType::NewProject:
    succeeded =
        session_.NewProject() == TrackerApplicationSession::LoadResult::Loaded;
    break;
  case Ui2ProjectLifecycleCommandType::LoadProject:
    if (FileSystem *fileSystem = FileSystem::GetInstance()) {
      succeeded = Ui2RecoverStagedProjectSampleDeletes(
                      *fileSystem, command.project.data()) &&
                  session_.LoadProject(command.project.data(), false, true) ==
                      TrackerApplicationSession::LoadResult::Loaded;
    }
    break;
  case Ui2ProjectLifecycleCommandType::DeleteProject:
    succeeded = session_.DeleteProject(command.project.data());
    break;
  case Ui2ProjectLifecycleCommandType::None:
  case Ui2ProjectLifecycleCommandType::OverwriteProject:
  case Ui2ProjectLifecycleCommandType::OverwriteTheme:
  case Ui2ProjectLifecycleCommandType::PurgeUnusedSamples:
  case Ui2ProjectLifecycleCommandType::PurgeUnusedInstruments:
    break;
  }
  autoSave_.SetPersistBusy(false);

  if (command.type == Ui2ProjectLifecycleCommandType::DeleteProject) {
    if (!succeeded) {
      projectLifecycle_.ReportFailure(
          Ui2ProjectLifecycleFailure::DeleteProject);
      return;
    }
    if (!projectBrowser_.Refresh(session_.ProjectName()))
      projectLifecycle_.ReportFailure(
          Ui2ProjectLifecycleFailure::RefreshBrowserAfterDelete);
    return;
  }

  if (!succeeded) {
    // Project loading and rollback both reuse FileSystem's global directory
    // listing. Rebuild the browser before leaving it visible behind the error
    // dialog; otherwise its saved indices point into the last sample/project
    // transaction listing and render as a selected blank row.
    if (command.type == Ui2ProjectLifecycleCommandType::LoadProject) {
      projectBrowser_.RefreshAndSelect(session_.ProjectName(),
                                       command.project.data());
    }
    projectLifecycle_.ReportFailure(
        command.type == Ui2ProjectLifecycleCommandType::NewProject
            ? Ui2ProjectLifecycleFailure::NewProject
            : Ui2ProjectLifecycleFailure::LoadProject,
        command.project.data());
    return;
  }

  std::snprintf(savedProjectName_.data(), savedProjectName_.size(), "%s",
                session_.ProjectName());
  projectSaveAsPending_ = false;
  autoSave_.SetSaveAsPending(false);
  ResetControllersAfterProjectBoundary();
  observedProjectMutationGeneration_ = modelPort_.ProjectMutationGeneration();
  if (command.type == Ui2ProjectLifecycleCommandType::NewProject)
    autoSave_.OnProjectCreated(nowMs);
  else
    autoSave_.OnProjectLoaded(nowMs);

  // A project boundary always opens Song after every controller has been
  // reconstructed from the restored model. Resetting only the Grid controller
  // leaves stale Project/Groove/Instrument selectors pointing into the prior
  // project and makes a real data restore appear name-only.
  ActivatePage(UiApplicationPage::Song);
}

void Ui2TrackerApplication::ResetControllersAfterProjectBoundary() {
  StopSamplePreview();
  if (sampleEditorTransaction_.Active())
    (void)sampleEditorTransaction_.Discard();
  sampleEditorTransaction_.Reset();
  if (sampleEditor_.Active())
    sampleEditor_.Close();
  if (sampleSlices_.Active())
    sampleSlices_.Close();
  std::array<Ui2SelectorState, static_cast<std::size_t>(Ui2DeviceField::Count)>
      deviceSelectors{};
  for (std::size_t index = 0U; index < deviceSelectors.size(); ++index) {
    deviceSelectors[index] =
        device_.Selector(static_cast<Ui2DeviceField>(index));
  }
  const std::uint32_t visibleDeviceFields = device_.VisibleFields();
  const std::uint8_t textCase = font_.TextCase();

  project_ = {};
  projectBrowser_ = {};
  clipboardNotice_.Clear();
  feedback_ = {};
  projectLifecycle_ = {};
  projectRender_.Reset();
  groove_ = {};
  grooveClipboard_ = {};
  device_ = {};
  deviceLifecycle_ = {};
  for (std::size_t index = 0U; index < deviceSelectors.size(); ++index) {
    device_.SetSelector(static_cast<Ui2DeviceField>(index),
                        deviceSelectors[index]);
  }
  device_.SetVisibleFields(visibleDeviceFields);
  theme_ = {};
  font_ = {};
  font_.SetTextCase(textCase);
  rename_ = {};
  renameTarget_ = RenameTarget::None;
  deferredProjectSave_.Cancel();
  mixer_ = {};
  instrument_ = {};
  instrumentLifecycle_ = {};
  instrumentBrowser_ = {};
  settingsBrowser_.Close();
  sampleBrowser_.Close();
  sampleReturnPage_ = UiApplicationPage::Instrument;
  instrumentBrowserActive_ = false;
  source_.SetInstrumentBrowserActive(false);
  record_ = {};
  recordSessionPending_ = false;
  ConfigureRecordController();
  projectInput_ = {};
  physicalHeldMask_ = 0U;
  pressOwners_.fill(UiApplicationPage::None);
  modelPort_.ResetProjectBoundary();
  tracker_.SynchronizeFromPort();
  SynchronizeNonGridNavigationHeld(false);
  tracker_.Hub().SetNavigationHeld(false);
  source_.SetNavigationHeld(false);
  runtime_.Invalidate();
}

void Ui2TrackerApplication::MarkProjectDirty() {
  modelPort_.MarkProjectMutated();
  SynchronizeProjectMutationState();
}

void Ui2TrackerApplication::SynchronizeProjectMutationState() {
  const std::uint32_t mutationGeneration =
      modelPort_.ProjectMutationGeneration();
  if (mutationGeneration == observedProjectMutationGeneration_)
    return;
  observedProjectMutationGeneration_ = mutationGeneration;
  autoSave_.MarkDirty(System::GetInstance()->Millis());
}

void Ui2TrackerApplication::ShowFeedbackError(const char *message) {
  System *system = System::GetInstance();
  feedback_.ShowError(message, system == nullptr ? 0U : system->Millis());
  runtime_.Invalidate();
}

void Ui2TrackerApplication::ShowFeedbackMessage(const char *message) {
  System *system = System::GetInstance();
  feedback_.ShowMessage(message, system == nullptr ? 0U : system->Millis());
  runtime_.Invalidate();
}

bool Ui2TrackerApplication::FlushConfig() {
  Config *config = Config::GetInstance();
  const bool saved = configSave_.Flush(
      [config]() { return config != nullptr && config->Save(); });
  if (!saved) {
    Status::Set("CONFIG SAVE FAILED");
    ShowFeedbackError("CONFIG SAVE FAILED");
  }
  return saved;
}

void Ui2TrackerApplication::ExecuteProject(Ui2ProjectCommand command) {
  switch (command.type) {
  case Ui2ProjectCommandType::SaveProject:
    if (deferredProjectSave_.Request(session_.ProjectName()) ==
        Ui2ProjectSaveStart::RenameFirst) {
      renameTarget_ = RenameTarget::Project;
      const Ui2ProjectNamePresentation presentation(session_.ProjectName());
      rename_.Begin(presentation.RenameDraft(), MAX_PROJECT_NAME_LENGTH,
                    &PersistencyService::IsValidProjectName,
                    TrackerAction::Enter);
      break;
    }
    SaveCurrentProject();
    break;
  case Ui2ProjectCommandType::RemoveUnusedSamples:
    projectLifecycle_.RequestPurgeUnusedSamples(
        Player::GetInstance()->IsAudioActive(), TrackerAction::Enter);
    break;
  case Ui2ProjectCommandType::RemoveUnusedInstruments:
    projectLifecycle_.RequestPurgeUnusedInstruments(
        Player::GetInstance()->IsAudioActive(), TrackerAction::Enter);
    break;
  case Ui2ProjectCommandType::AdjustTempo:
  case Ui2ProjectCommandType::AdjustTranspose:
  case Ui2ProjectCommandType::AdjustScale:
  case Ui2ProjectCommandType::AdjustRoot: {
    Project &project = session_.ProjectModel();
    const Ui2ProjectValuePlan plan = Ui2ProjectWorkflow::ValuePlan(command);
    if (Variable *variable = project.FindVariable(plan.variable)) {
      const int previous = variable->GetInt();
      const int adjusted = Ui2ProjectWorkflow::ApplyValue(previous, plan);
      if (adjusted == previous)
        break;
      variable->SetInt(adjusted);
      MarkProjectDirty();
    }
    break;
  }
  case Ui2ProjectCommandType::NewProject:
    ExecuteProjectLifecycle(projectLifecycle_.RequestNew(
        autoSave_.Dirty(), Player::GetInstance()->IsRunning(),
        TrackerAction::Enter));
    break;
  case Ui2ProjectCommandType::RenameProject:
    deferredProjectSave_.Cancel();
    renameTarget_ = RenameTarget::Project;
    rename_.Begin(
        Ui2ProjectNamePresentation(session_.ProjectName()).RenameDraft(),
        MAX_PROJECT_NAME_LENGTH, &PersistencyService::IsValidProjectName,
        TrackerAction::Enter);
    break;
  case Ui2ProjectCommandType::LoadProject:
    if (projectSaveAsPending_) {
      projectLifecycle_.WarnPendingRename();
      break;
    }
    if (!projectBrowser_.Refresh(session_.ProjectName())) {
      projectLifecycle_.ReportFailure(
          Ui2ProjectLifecycleFailure::OpenProjectBrowser);
      break;
    }
    instrumentBrowserActive_ = false;
    source_.SetInstrumentBrowserActive(false);
    settingsBrowser_.Close();
    ActivatePage(UiApplicationPage::Browser);
    break;
  case Ui2ProjectCommandType::BrowseSamplePool: {
    if (Player::GetInstance()->IsRunning()) {
      projectLifecycle_.ReportRunningBlocked();
      break;
    }
    if (!sampleBrowser_.Open(session_.ProjectName()))
      break;
    settingsBrowser_.Close();
    instrumentBrowserActive_ = false;
    source_.SetInstrumentBrowserActive(false);
    ActivatePage(UiApplicationPage::Browser);
    break;
  }
  case Ui2ProjectCommandType::RenderMixdown:
    if (!projectRender_.Request(Ui2ProjectRenderMode::Mixdown) &&
        projectRender_.LastStartResult() ==
            Ui2ProjectRenderStartResult::PlayerBusy)
      ShowFeedbackMessage("STOP PLAYBACK TO RENDER");
    break;
  case Ui2ProjectCommandType::RenderStems:
    if (!projectRender_.Request(Ui2ProjectRenderMode::Stems) &&
        projectRender_.LastStartResult() ==
            Ui2ProjectRenderStartResult::PlayerBusy)
      ShowFeedbackMessage("STOP PLAYBACK TO RENDER");
    break;
  case Ui2ProjectCommandType::None:
    break;
  }
}

void Ui2TrackerApplication::ExecuteGroove(Ui2GrooveCommand command) {
  if (!command.HasValue())
    return;
  const std::uint8_t clipboardCountBefore = grooveClipboard_.count;
  const std::uint8_t number = groove_.Number();
  std::uint8_t *steps = Groove::GetInstance()->GetGrooveData(number);
  const Ui2GrooveWorkflowResult result =
      Ui2GrooveWorkflow::Execute(command, steps, grooveClipboard_);
  if (result.projectMutated)
    MarkProjectDirty();
  if (result.selectNumber)
    session_.EditorState().currentGroove_ = groove_.Number();
  if (result.dispatchPerformance) {
    const Ui2TrackerCommand trackerCommand = Ui2GrooveTrackerCommand(
        command, session_.EditorState().songX_);
    modelPort_.ApplyGridCommand(trackerCommand);
  }
  System *system = System::GetInstance();
  const std::uint32_t nowMs = system == nullptr ? 0U : system->Millis();
  if (command.type == Ui2GrooveCommandType::CopySelection ||
      command.type == Ui2GrooveCommandType::CutSelection) {
    if (grooveClipboard_.count != 0U)
      clipboardNotice_.ShowCopied(1U, grooveClipboard_.count, nowMs);
  } else if (command.type == Ui2GrooveCommandType::PasteSelection &&
             clipboardCountBefore != 0U) {
    clipboardNotice_.ShowPasted(1U, clipboardCountBefore, nowMs);
  } else if (command.type == Ui2GrooveCommandType::InterpolateSelection &&
             result.interpolationApplied && command.selection.active) {
    const std::uint8_t stepCount = static_cast<std::uint8_t>(
        command.selection.Bottom() - command.selection.Top() + 1U);
    clipboardNotice_.ShowInterpolated(stepCount, nowMs);
  }
}

void Ui2TrackerApplication::SynchronizeGridPage() {
  const UiApplicationPage page = tracker_.ActiveApplicationPage();
  if (page != UiApplicationPage::None && page != activePage_)
    ActivatePage(page);
}

} // namespace ui2
