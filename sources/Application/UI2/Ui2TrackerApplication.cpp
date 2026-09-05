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
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SoundSource.h"
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
void StopPreviewTransport(Ui2SampleWorkflow::PreviewKind kind,
                          std::uint8_t instrument) {
  if (Player *player = Player::GetInstance()) {
    if (kind == Ui2SampleWorkflow::PreviewKind::EditorStream)
      player->StopStreaming();
    else if (kind == Ui2SampleWorkflow::PreviewKind::SliceNote)
      player->StopNote(instrument, SONG_CHANNEL_COUNT - 1U);
  }
}

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
  Ui2ToggleSongTransportAtCursor(*Player::GetInstance(), PM_SONG,
                                 session.EditorState().songX_,
                                 static_cast<std::uint8_t>(SONG_CHANNEL_COUNT));
}

Ui2InstrumentParameterDescriptor
ActiveInstrumentParameter(TrackerApplicationSession &session,
                          Ui2InstrumentCursorPosition cursor) {
  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  if (bank == nullptr)
    return {};
  const auto slot = static_cast<unsigned short>(std::clamp(
      session.EditorState().currentInstrumentID_, 0, MAX_INSTRUMENT_COUNT - 1));
  I_Instrument *instrument = bank->GetInstrument(slot);
  if (instrument == nullptr)
    return {};
  const InstrumentType type = instrument->GetType();
  const bool sidFirstChip =
      type != IT_SID ||
      static_cast<SIDInstrument *>(instrument)->GetChip() == SID1;
  Ui2InstrumentParameterDescriptor descriptor =
      Ui2InstrumentCursorParameter(type, cursor, sidFirstChip);
  if (type == IT_SAMPLE && Ui2IsSamplePositionParameter(descriptor)) {
    descriptor = Ui2ResolveSamplePositionMaximum(
        descriptor,
        static_cast<SampleInstrument *>(instrument)->GetSampleSize());
  }
  return descriptor;
}

void ConfigureInstrumentSubfields(TrackerApplicationSession &session,
                                  Ui2InstrumentController &controller) {
  const Ui2InstrumentSubfieldSpec spec = Ui2InstrumentSubfields(
      ActiveInstrumentParameter(session, controller.Cursor()));
  controller.ConfigureValueSubfields(spec.mode, spec.count);
}

TrackerAction
InstrumentTypeChangeTrigger(Ui2InstrumentValueDirection direction) {
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
      projects_(session_.ProjectModel(), session_.EditorState()),
      samples_(&StopPreviewTransport),
      source_(session_, tracker_, projects_.controller, projects_.browser,
              settingsBrowser_, clipboardNotice_, feedback_,
              projects_.lifecycle, projects_.render, groove_, grooveClipboard_,
              device_, deviceLifecycle_, theme_, font_, rename_, mixer_,
              instrument_, instrumentLifecycle_, instrumentBrowser_,
              samples_.browser, samples_.editor, samples_.slices, record_,
              firmwareLifecycle_, persistenceStatus_),
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
  projects_.deferredSave.Cancel();

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
    font_.SetTextCase(static_cast<std::uint8_t>(configValue(
        FourCC::VarUITextCase, 1, Ui2FontController::TextCaseCount - 1U)));
    if (Variable *brightness =
            config->FindVariable(FourCC::VarBacklightLevel)) {
      const int configuredBrightness = brightness->GetInt();
      const int rawBrightness =
          std::clamp(configuredBrightness,
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
      ~(std::uint32_t{1} << static_cast<std::uint8_t>(Ui2DeviceField::LineOut));
  // Node hardware does not expose the bootloader action; other firmware
  // targets retain the guarded UPDATE FIRMWARE row.
  visibleDeviceFields &= ~(std::uint32_t{1} << static_cast<std::uint8_t>(
                               Ui2DeviceField::UpdateFirmware));
#if defined(NULLPERATOR_IOS)
  visibleDeviceFields &= ~(std::uint32_t{1} << static_cast<std::uint8_t>(
                               Ui2DeviceField::MidiDevice));
  visibleDeviceFields &=
      ~(std::uint32_t{1} << static_cast<std::uint8_t>(Ui2DeviceField::Volume));
  visibleDeviceFields &= ~(std::uint32_t{1} << static_cast<std::uint8_t>(
                               Ui2DeviceField::Brightness));
#endif
  device_.SetVisibleFields(visibleDeviceFields);
  ConfigureRecordController();
  ActivatePage(UiApplicationPage::Song);
  return true;
}

void Ui2TrackerApplication::Shutdown() {
  projects_.deferredSave.Cancel();
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
  if (IsRecordingActive() || IsSavingRecording())
    StopRecording();
  StopMonitoring();
  samples_.Reset();
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
  projects_.input.SetNavigationHeld(held);
  device_.SetNavigationHeld(held);
  theme_.SetNavigationHeld(held);
  font_.SetNavigationHeld(held);
  groove_.SetNavigationHeld(held);
  mixer_.SetNavigationHeld(held);
  instrument_.SetNavigationHeld(held);
  samples_.browser.SetNavigationHeld(held);
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
  if (projects_.render.Active()) {
    projects_.render.Handle(action, pressed);
    finishModalRelease();
    return;
  }
  if (projects_.lifecycle.Active()) {
    HandleProjectLifecycle(action, pressed);
    finishModalRelease();
    return;
  }
  if (samples_.browser.DialogActive()) {
    HandleSampleBrowserDialog(action, pressed);
    finishModalRelease();
    return;
  }
  if (samples_.editor.DialogActive()) {
    HandleSampleEditorDialog(action, pressed);
    finishModalRelease();
    return;
  }
  if (samples_.slices.DialogActive()) {
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
      target = samples_.returnPage;
    break;
  case UiApplicationPage::Record:
  case UiApplicationPage::None:
    break;
  }
  if (target == UiApplicationPage::None)
    return true;

  if (activePage_ == UiApplicationPage::Project &&
      target != UiApplicationPage::Project && projects_.saveAsPending) {
    projects_.lifecycle.WarnPendingRename();
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
  projects_.render.Tick();
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
      .operationAllowsSave =
          AutosaveSafePage() && !projects_.render.Active() &&
          !projects_.lifecycle.Active() && !deviceLifecycle_.Active() &&
          !instrumentLifecycle_.Active() && !rename_.Active(),
  };
}

bool Ui2TrackerApplication::ActivatePage(UiApplicationPage page) {
  if (page == UiApplicationPage::None)
    return false;
  if (page != activePage_ && samples_.transaction.ApplyActive())
    return false;
  if (activePage_ == UiApplicationPage::Project &&
      page != UiApplicationPage::Project && projects_.saveAsPending) {
    projects_.lifecycle.WarnPendingRename();
    return false;
  }
  const bool changed = activePage_ != page;
  if (changed && activePage_ == UiApplicationPage::Record &&
      (IsRecordingActive() || IsSavingRecording())) {
    ShowFeedbackMessage("STOP RECORDING FIRST");
    return false;
  }
  if (changed)
    projects_.deferredSave.Cancel();
  if (changed && activePage_ == UiApplicationPage::SampleEditor &&
      page != UiApplicationPage::SampleEditor && samples_.editor.Active()) {
    if (!CloseSampleEditor())
      return false;
  }
  if (changed && activePage_ == UiApplicationPage::SampleSlices &&
      page != UiApplicationPage::SampleSlices && samples_.slices.Active()) {
    StopSamplePreview();
    samples_.slices.Close();
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

bool Ui2TrackerApplication::ActivateDiagnosticTable(Ui2TrackerPage tablePage) {
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
    if (!projects_.browser.Refresh(session_.ProjectName()))
      projects_.lifecycle.ReportFailure(
          Ui2ProjectLifecycleFailure::OpenProjectBrowser);
    break;
  case Ui2DiagnosticBrowser::Instrument:
    if (!instrumentBrowser_.Refresh())
      instrumentBrowser_.SetError("INSTRUMENT BROWSER FAILED");
    instrumentBrowserActive_ = true;
    source_.SetInstrumentBrowserActive(true);
    break;
  case Ui2DiagnosticBrowser::SampleImport: {
    if (!samples_.browser.OpenLibrary(session_.ProjectName()))
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
  if (!projects_.input.Update(action, pressed))
    return;
  projects_.controller.SetEnterHeld(projects_.input.Held(TrackerAction::Enter));
  if (!pressed)
    return;

  if (projects_.input.Held(TrackerAction::Shift) ||
      projects_.input.Held(TrackerAction::Option)) {
    return;
  }
  if (projects_.input.Held(TrackerAction::Enter)) {
    if (action == TrackerAction::Enter)
      ExecuteProject(projects_.controller.Enter());
    else if ((projects_.controller.ContentCursor() ==
                  Ui2ProjectContentCursor::Tempo ||
              projects_.controller.ContentCursor() ==
                  Ui2ProjectContentCursor::Transpose ||
              projects_.controller.ContentCursor() ==
                  Ui2ProjectContentCursor::Scale ||
              projects_.controller.ContentCursor() ==
                  Ui2ProjectContentCursor::Root) &&
             (action == TrackerAction::Left || action == TrackerAction::Right ||
              action == TrackerAction::Up || action == TrackerAction::Down))
      ExecuteProject(projects_.controller.Adjust(action));
    return;
  }
  if (projects_.input.AnyModifier())
    return;

  // Value rows always expose fine horizontal editing. Holding ENTER adds the
  // vertical coarse path, but is not a prerequisite for ordinary +/-1.
  if (action == TrackerAction::Left || action == TrackerAction::Right) {
    const Ui2ProjectCommand adjustment = projects_.controller.Adjust(action);
    if (adjustment.HasValue()) {
      ExecuteProject(adjustment);
      return;
    }
  }

  switch (action) {
  case TrackerAction::Up:
    projects_.controller.MoveUp();
    break;
  case TrackerAction::Down:
    projects_.controller.MoveDown();
    break;
  case TrackerAction::Left:
    projects_.controller.MoveLeft();
    break;
  case TrackerAction::Right:
    projects_.controller.MoveRight();
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
      const bool persisted = config != nullptr &&
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
          return persistence->ImportInstrument(
                     candidate, command.filename.data()) == PERSIST_LOADED;
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
  if (samples_.browser.Active()) {
    ExecuteSampleBrowser(samples_.browser.Handle(action, pressed));
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
      projects_.browser.Handle(action, pressed);
  Ui2ProjectLifecycleCommand lifecycleCommand;
  if (command.type == Ui2ProjectBrowserCommandType::Load) {
    lifecycleCommand = projects_.lifecycle.RequestLoad(
        command.project.data(), autoSave_.Dirty(),
        Player::GetInstance()->IsRunning(), TrackerAction::Enter);
  } else if (command.type == Ui2ProjectBrowserCommandType::Delete) {
    lifecycleCommand = projects_.lifecycle.RequestDelete(
        command.project.data(), session_.ProjectName(),
        Player::GetInstance()->IsRunning(), TrackerAction::Enter);
  }
  ExecuteProjectLifecycle(lifecycleCommand);
}

UiApplicationPage Ui2TrackerApplication::BrowserReturnPage() const {
  if (settingsBrowser_.Mode() == Ui2SettingsBrowserMode::Theme)
    return UiApplicationPage::Theme;
  if (samples_.browser.Active())
    return UiApplicationPage::Project;
  return instrumentBrowserActive_ ? UiApplicationPage::Instrument
                                  : UiApplicationPage::Project;
}

void Ui2TrackerApplication::HandleProjectLifecycle(TrackerAction action,
                                                   bool pressed) {
  ExecuteProjectLifecycle(projects_.lifecycle.Handle(action, pressed));
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
    deviceLifecycle_.RequestUpdateFirmware(Player::GetInstance()->IsRunning(),
                                           TrackerAction::Enter);
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
          bank == nullptr ? nullptr
                          : bank->GetInstrument(renameInstrumentNumber_);
      if (instrument != nullptr) {
        instrument->SetName(rename_.Value());
        MarkProjectDirty();
      }
    } else if (renameTarget_ == RenameTarget::Project) {
      const bool saveAfterRename = projects_.deferredSave.CompleteRename();
      projects_.saveAsPending =
          std::strcmp(savedProjectName_.data(), rename_.Value()) != 0;
      session_.ProjectModel().SetProjectName(rename_.Value());
      autoSave_.SetSaveAsPending(projects_.saveAsPending);
      MarkProjectDirty();
      renameTarget_ = RenameTarget::None;
      if (saveAfterRename)
        SaveCurrentProject();
      return;
    } else if (renameTarget_ == RenameTarget::Theme ||
               renameTarget_ == RenameTarget::NewTheme) {
      CommitThemeName(rename_.Value(), renameTarget_ == RenameTarget::NewTheme);
    }
    renameTarget_ = RenameTarget::None;
  } else if (command == Ui2RenameCommand::Cancel) {
    projects_.deferredSave.Cancel();
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
          Ui2InstrumentSampleOpenOutcomeFor(sample->GetSampleIndex(),
                                            !filename.empty(),
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
        (void)OpenSampleSlices(filename.c_str(), UiApplicationPage::Instrument);
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
                           ? Ui2AdjustInstrumentParameter(descriptor, previous,
                                                          command.direction)
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
  const auto slot =
      static_cast<unsigned short>(session_.EditorState().currentInstrumentID_);
  const Ui2InstrumentTypeOutcome result =
      Ui2InstrumentWorkflow::ChangeType(bank, slot, command.instrumentType,
                                        Player::GetInstance()->IsAudioActive());
  if (result == Ui2InstrumentTypeOutcome::PlayingBlocked) {
    I_Instrument *current =
        bank == nullptr ? nullptr : bank->GetInstrument(slot);
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
  const auto slot =
      static_cast<unsigned short>(session_.EditorState().currentInstrumentID_);
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
  record_.SetSourceSelectable(IsRecordingInputSelectable());
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
    constexpr const char *recordingPath = RECORDINGS_DIR "/" RECORDING_FILENAME;
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
  constexpr const char *recordingPath = RECORDINGS_DIR "/" RECORDING_FILENAME;
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
    rename_.Begin("New Theme", MAX_THEME_NAME_LENGTH, &Config::IsValidThemeName,
                  TrackerAction::Enter);
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
        projects_.lifecycle.ReportFailure(
            Ui2ProjectLifecycleFailure::SaveTheme);
      } else if (fileSystem->exists(path.data())) {
        // Match the legacy flow: an existing theme is never silently replaced.
        // The shared conservative message dialog defaults to NO.
        projects_.lifecycle.RequestThemeOverwrite(name->GetString().c_str(),
                                                  TrackerAction::Enter);
      } else if (!config->ExportTheme(name->GetString().c_str(), false)) {
        projects_.lifecycle.ReportFailure(
            Ui2ProjectLifecycleFailure::SaveTheme);
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
    const bool saved = session_.AutoSave(conditions.operationAllowsSave,
                                         conditions.recordingActive);
    autoSave_.CompleteAutoSave(nowMs, saved);
    persistenceStatus_.FinishSaving(nowMs);
    if (!saved)
      projects_.lifecycle.ReportFailure(
          Ui2ProjectLifecycleFailure::SaveProject);
    return;
  }
  if (kind != PendingSaveKind::Project) {
    persistenceStatus_.FinishSaving(nowMs);
    return;
  }

  const TrackerApplicationSession::SaveResult result = session_.SaveProject(
      savedProjectName_.data(), projects_.saveAsPending, overwrite);
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
    projects_.lifecycle.RequestOverwrite(session_.ProjectName(), trigger);
    return;
  }
  if (result != TrackerApplicationSession::SaveResult::Saved) {
    projects_.lifecycle.ReportFailure(Ui2ProjectLifecycleFailure::SaveProject);
    return;
  }
  std::snprintf(savedProjectName_.data(), savedProjectName_.size(), "%s",
                session_.ProjectName());
  projects_.saveAsPending = false;
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
      projects_.lifecycle.ReportFailure(Ui2ProjectLifecycleFailure::SaveTheme);
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
      command.type == Ui2ProjectLifecycleCommandType::PurgeUnusedInstruments) {
    // Recheck at execution time as well as when opening the confirmation. A
    // transport may start outside this modal's input path, and neither sample
    // files nor live instrument slots may be released while Player owns them.
    if (Player::GetInstance()->IsAudioActive()) {
      if (command.type == Ui2ProjectLifecycleCommandType::PurgeUnusedSamples)
        projects_.lifecycle.RequestPurgeUnusedSamples(true,
                                                      TrackerAction::Enter);
      else
        projects_.lifecycle.RequestPurgeUnusedInstruments(true,
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
      projects_.lifecycle.ReportFailure(
          Ui2ProjectLifecycleFailure::DeleteProject);
      return;
    }
    if (!projects_.browser.Refresh(session_.ProjectName()))
      projects_.lifecycle.ReportFailure(
          Ui2ProjectLifecycleFailure::RefreshBrowserAfterDelete);
    return;
  }

  if (!succeeded) {
    // Project loading and rollback both reuse FileSystem's global directory
    // listing. Rebuild the browser before leaving it visible behind the error
    // dialog; otherwise its saved indices point into the last sample/project
    // transaction listing and render as a selected blank row.
    if (command.type == Ui2ProjectLifecycleCommandType::LoadProject) {
      projects_.browser.RefreshAndSelect(session_.ProjectName(),
                                         command.project.data());
    }
    projects_.lifecycle.ReportFailure(
        command.type == Ui2ProjectLifecycleCommandType::NewProject
            ? Ui2ProjectLifecycleFailure::NewProject
            : Ui2ProjectLifecycleFailure::LoadProject,
        command.project.data());
    return;
  }

  std::snprintf(savedProjectName_.data(), savedProjectName_.size(), "%s",
                session_.ProjectName());
  projects_.saveAsPending = false;
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
  samples_.Reset();
  projects_.Reset();
  std::array<Ui2SelectorState, static_cast<std::size_t>(Ui2DeviceField::Count)>
      deviceSelectors{};
  for (std::size_t index = 0U; index < deviceSelectors.size(); ++index) {
    deviceSelectors[index] =
        device_.Selector(static_cast<Ui2DeviceField>(index));
  }
  const std::uint32_t visibleDeviceFields = device_.VisibleFields();
  const std::uint8_t textCase = font_.TextCase();

  clipboardNotice_.Clear();
  feedback_ = {};
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
  mixer_ = {};
  instrument_ = {};
  instrumentLifecycle_ = {};
  instrumentBrowser_ = {};
  settingsBrowser_.Close();
  instrumentBrowserActive_ = false;
  source_.SetInstrumentBrowserActive(false);
  record_ = {};
  recordSessionPending_ = false;
  ConfigureRecordController();
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
    if (projects_.deferredSave.Request(session_.ProjectName()) ==
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
    projects_.lifecycle.RequestPurgeUnusedSamples(
        Player::GetInstance()->IsAudioActive(), TrackerAction::Enter);
    break;
  case Ui2ProjectCommandType::RemoveUnusedInstruments:
    projects_.lifecycle.RequestPurgeUnusedInstruments(
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
    ExecuteProjectLifecycle(projects_.lifecycle.RequestNew(
        autoSave_.Dirty(), Player::GetInstance()->IsRunning(),
        TrackerAction::Enter));
    break;
  case Ui2ProjectCommandType::RenameProject:
    projects_.deferredSave.Cancel();
    renameTarget_ = RenameTarget::Project;
    rename_.Begin(
        Ui2ProjectNamePresentation(session_.ProjectName()).RenameDraft(),
        MAX_PROJECT_NAME_LENGTH, &PersistencyService::IsValidProjectName,
        TrackerAction::Enter);
    break;
  case Ui2ProjectCommandType::LoadProject:
    if (projects_.saveAsPending) {
      projects_.lifecycle.WarnPendingRename();
      break;
    }
    if (!projects_.browser.Refresh(session_.ProjectName())) {
      projects_.lifecycle.ReportFailure(
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
      projects_.lifecycle.ReportRunningBlocked();
      break;
    }
    if (!samples_.browser.Open(session_.ProjectName()))
      break;
    settingsBrowser_.Close();
    instrumentBrowserActive_ = false;
    source_.SetInstrumentBrowserActive(false);
    ActivatePage(UiApplicationPage::Browser);
    break;
  }
  case Ui2ProjectCommandType::RenderMixdown:
    if (!projects_.render.Request(Ui2ProjectRenderMode::Mixdown) &&
        projects_.render.LastStartResult() ==
            Ui2ProjectRenderStartResult::PlayerBusy)
      ShowFeedbackMessage("STOP PLAYBACK TO RENDER");
    break;
  case Ui2ProjectCommandType::RenderStems:
    if (!projects_.render.Request(Ui2ProjectRenderMode::Stems) &&
        projects_.render.LastStartResult() ==
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
    const Ui2TrackerCommand trackerCommand =
        Ui2GrooveTrackerCommand(command, session_.EditorState().songX_);
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
