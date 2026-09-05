/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2BrightnessMapping.h"
#include "Application/UI2/Ui2DeviceLifecycleService.h"
#include "Application/UI2/Ui2GrooveCommandAdapter.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Application/UI2/Ui2InstrumentTableAllocation.h"
#include "Application/UI2/Ui2ProjectNamePresentation.h"
#include "Application/UI2/Ui2SampleFileOperations.h"
#include "Application/UI2/Ui2TrackerApplication.h"
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
SampleInstrument *CurrentSampleInstrument(TrackerApplicationSession &session) {
  InstrumentBank *bank = session.ProjectModel().GetInstrumentBank();
  if (bank == nullptr)
    return nullptr;
  const auto slot = static_cast<unsigned short>(std::clamp(
      session.EditorState().currentInstrumentID_, 0, MAX_INSTRUMENT_COUNT - 1));
  I_Instrument *instrument = bank->GetInstrument(slot);
  return instrument != nullptr && instrument->GetType() == IT_SAMPLE
             ? static_cast<SampleInstrument *>(instrument)
             : nullptr;
}

} // namespace

void Ui2TrackerApplication::HandleSampleBrowserDialog(TrackerAction action,
                                                      bool pressed) {
  ExecuteSampleBrowser(samples_.browser.HandleDialog(action, pressed));
}

void Ui2TrackerApplication::CloseSampleBrowser() {
  if (!samples_.browser.Active())
    return;
  Player *player = Player::GetInstance();
  if (player != nullptr && !player->IsRunning() && player->IsPlaying())
    player->StopStreaming();
  samples_.browser.Close();
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
      samples_.browser.SetError("PREVIEW UNAVAILABLE");
      return;
    }
    WavFile wave;
    const auto opened = wave.Open(command.filename.data());
    if (!opened) {
      samples_.browser.SetError("INVALID SAMPLE");
      return;
    }
    wave.Close();
    if (player->IsPlaying())
      player->StopStreaming();
    if (command.singleCycle)
      player->StartLoopingStreaming(command.filename.data());
    else
      player->StartStreaming(command.filename.data());
    samples_.browser.ClearError();
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
      samples_.browser.SetError("INVALID SAMPLE");
      return;
    }
    samples_.browser.ClearError();
    return;
  }

  if (command.type == Ui2SampleBrowserCommandType::RequestDelete) {
    if (!command.projectSample || player == nullptr || player->IsRunning() ||
        player->IsPlaying() || pool == nullptr) {
      samples_.browser.SetError("DELETE UNAVAILABLE");
      return;
    }
    Project &project = session_.ProjectModel();
    if (project.SampleInUse(etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(
            command.filename.data()))) {
      samples_.browser.SetError("SAMPLE IN USE");
      return;
    }
    samples_.browser.RequestDeleteConfirmation(command.filename.data(),
                                               TrackerAction::Enter);
    return;
  }

  if (command.type == Ui2SampleBrowserCommandType::DeleteConfirmed) {
    if (player == nullptr || player->IsRunning() || player->IsPlaying() ||
        fileSystem == nullptr || pool == nullptr ||
        session_.ProjectModel().SampleInUse(
            etl::string<MAX_INSTRUMENT_FILENAME_LENGTH>(
                command.filename.data()))) {
      samples_.browser.SetError("DELETE UNAVAILABLE");
      return;
    }
    const Ui2DeleteProjectSampleResult result = Ui2DeleteProjectSampleSafely(
        *fileSystem, *pool, session_.ProjectName(), command.filename.data());
    if (result == Ui2DeleteProjectSampleResult::Deleted ||
        result == Ui2DeleteProjectSampleResult::CleanupFailed) {
      samples_.browser.RefreshCurrentDirectory();
      if (result == Ui2DeleteProjectSampleResult::CleanupFailed)
        samples_.browser.SetError("DELETE CLEANUP FAILED");
      MarkProjectDirty();
    } else if (result == Ui2DeleteProjectSampleResult::UnloadFailed) {
      samples_.browser.SetError("DELETE UNSUPPORTED");
    } else if (result == Ui2DeleteProjectSampleResult::RollbackFailed) {
      samples_.browser.SetError("DELETE RECOVERY FAILED");
    } else {
      samples_.browser.SetError("DELETE FAILED");
    }
    return;
  }

  if (command.type != Ui2SampleBrowserCommandType::Import)
    return;
  if (player == nullptr || player->IsRunning() || player->IsPlaying() ||
      fileSystem == nullptr || pool == nullptr || command.projectSample ||
      command.filename[0] == '\0') {
    samples_.browser.SetError("IMPORT UNAVAILABLE");
    return;
  }
  const char *error = nullptr;
  if (!ImportSampleToCurrentInstrument(command.filename.data(), error)) {
    samples_.browser.SetError(error);
    return;
  }
  samples_.browser.ClearError();
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
  const std::uint8_t instrumentNumber =
      static_cast<std::uint8_t>(session_.EditorState().currentInstrumentID_);
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

bool Ui2TrackerApplication::OpenSampleEditor(const char *path, bool projectPool,
                                             UiApplicationPage returnPage) {
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr || path == nullptr || path[0] == '\0')
    return false;
  StopSamplePreview();
  if (samples_.editor.Active() && !CloseSampleEditor())
    return false;
  if (samples_.slices.Active())
    samples_.slices.Close();

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
  if (samples_.transaction.Begin(*fileSystem, destination) !=
      Ui2SampleEditorTransactionResult::Ready)
    return false;

  const Ui2SampleWaveformLoadResult result =
      projectPool ? samples_.editor.OpenProjectPool(
                        *fileSystem, session_.ProjectName(), path)
                  : samples_.editor.OpenPath(*fileSystem, path, false);
  if (result != Ui2SampleWaveformLoadResult::Loaded) {
    (void)samples_.transaction.Discard();
    samples_.transaction.Reset();
    samples_.editor.Close();
    return false;
  }
  // Expose only the transactional same-name rewrite. Pool-aware rename is not
  // part of the editor contract until references and collisions are atomic.
  samples_.editor.SetTransactionCapabilities(true);
  samples_.returnPage = returnPage;
  if (!ActivatePage(UiApplicationPage::SampleEditor)) {
    (void)CloseSampleEditor();
    return false;
  }
  return true;
}

bool Ui2TrackerApplication::CloseSampleEditor() {
  if (samples_.transaction.ApplyActive())
    return false;
  StopSamplePreview();
  if (samples_.transaction.Active() &&
      samples_.transaction.Discard() !=
          Ui2SampleEditorTransactionResult::Discarded) {
    ShowFeedbackError("SAMPLE DISCARD FAILED");
    return false;
  }
  samples_.transaction.Reset();
  samples_.editor.Close();
  return true;
}

bool Ui2TrackerApplication::RecoverSampleEditorDestination() {
  StopSamplePreview();
  const auto failClosed = [this]() {
    // Journal files are intentionally left in place when cleanup/recovery
    // itself fails. A later Begin() can recover the destination without
    // exposing a controller whose waveform path may no longer exist.
    samples_.transaction.Reset();
    samples_.editor.Close();
    (void)ActivatePage(samples_.returnPage);
    ShowFeedbackError("SAMPLE RECOVERY FAILED");
    return false;
  };

  FileSystem *fileSystem = FileSystem::GetInstance();
  std::array<char, PFILENAME_SIZE> destination{};
  const int written =
      std::snprintf(destination.data(), destination.size(), "%s",
                    samples_.transaction.DestinationPath());
  if (fileSystem == nullptr || written <= 0 ||
      static_cast<std::size_t>(written) >= destination.size())
    return failClosed();

  // Reopen the journal before deleting anything. In the rollback-failure
  // state the backup and working files may be the only recovery evidence.
  if (samples_.transaction.Begin(*fileSystem, destination.data()) !=
          Ui2SampleEditorTransactionResult::Ready ||
      !samples_.editor.ReloadPath(*fileSystem, destination.data())) {
    (void)samples_.transaction.Discard();
    return failClosed();
  }
  samples_.editor.SetTransactionCapabilities(true);
  return true;
}

bool Ui2TrackerApplication::OpenSampleSlices(const char *path,
                                             UiApplicationPage returnPage) {
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr || path == nullptr || path[0] == '\0')
    return false;
  StopSamplePreview();
  if (samples_.editor.Active() && !CloseSampleEditor())
    return false;
  if (samples_.slices.Active())
    samples_.slices.Close();
  if (samples_.slices.OpenProjectPool(*fileSystem, session_.ProjectName(),
                                      path) !=
      Ui2SampleWaveformLoadResult::Loaded)
    return false;
  samples_.returnPage = returnPage;
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
  ExecuteSampleEditor(samples_.editor.Handle(action, pressed));
}

void Ui2TrackerApplication::HandleSampleEditorDialog(TrackerAction action,
                                                     bool pressed) {
  ExecuteSampleEditor(samples_.editor.HandleDialog(action, pressed));
}

bool Ui2TrackerApplication::ReloadSampleEditorTransactionView() {
  FileSystem *fileSystem = FileSystem::GetInstance();
  if (fileSystem == nullptr || !samples_.editor.Active() ||
      !samples_.transaction.Active())
    return false;
  const char *const path = samples_.transaction.HasWorkingCopy()
                               ? samples_.transaction.WorkingPath()
                               : samples_.transaction.DestinationPath();
  return samples_.editor.ReloadPath(*fileSystem, path);
}

void Ui2TrackerApplication::CompleteSampleEditorApply(
    Ui2SampleEditorTransactionResult result) {
  samples_.editor.FinishApplyProgress();
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
  if (!samples_.transaction.ApplyActive())
    return;
  const Ui2SampleEditorTransactionResult result =
      samples_.transaction.StepApply();
  samples_.editor.UpdateApplyProgress(samples_.transaction.ApplyProgress());
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
        static_cast<std::uint64_t>(samples_.waveform.FrameCount()) *
                samples_.waveform.ChannelCount() <=
            Ui2SingleCycleMaximumFrames;
    if (singleCycle)
      player->StartLoopingStreaming(command.path.data());
    else
      player->StartStreaming(command.path.data(),
                             static_cast<int>(command.start));
    // StopSamplePreview() clears any prior controller projection as well as
    // the audio owner. Re-arm the controller that owns this new command.
    samples_.editor.StartPreview(singleCycle ? 0U : command.start);
    samples_.preview.kind = SamplePreviewKind::EditorStream;
    samples_.preview.startedMs = System::GetInstance()->Millis();
    samples_.preview.frames = samples_.waveform.FrameCount();
    samples_.preview.start = singleCycle ? 0U : command.start;
    samples_.preview.end = singleCycle && samples_.preview.frames != 0U
                               ? samples_.preview.frames - 1U
                               : command.end;
    // AudioFileStreamer intentionally maps one single-cycle buffer to C4
    // (261.63 cycles/s), independent of the WAV header rate.
    samples_.preview.rate =
        singleCycle ? static_cast<std::uint32_t>(
                          (static_cast<std::uint64_t>(samples_.preview.frames) *
                               26163U +
                           50U) /
                          100U)
                    : samples_.waveform.SampleRate();
    samples_.preview.singleCycle = singleCycle;
    break;
  }
  case Ui2SampleEditorCommandType::PreviewStop:
    StopSamplePreview();
    break;
  case Ui2SampleEditorCommandType::NavigateBack:
  case Ui2SampleEditorCommandType::RequestDiscard:
    (void)ActivatePage(samples_.returnPage);
    break;
  case Ui2SampleEditorCommandType::SetStart:
  case Ui2SampleEditorCommandType::SetEnd:
    // START/END are an editor transaction, not live Instrument parameters.
    // They stay local until SAVE/APPLY succeeds, matching the legacy editor.
    break;
  case Ui2SampleEditorCommandType::RequestApplyOperation:
    StopSamplePreview();
    samples_.editor.RequestApplyConfirmation(command.operation, command.start,
                                             command.end, TrackerAction::Enter);
    break;
  case Ui2SampleEditorCommandType::ApplyConfirmed: {
    StopSamplePreview();
    const Ui2SampleEditorTransactionResult result =
        command.operation == Ui2SampleEditorOperation::Trim
            ? samples_.transaction.BeginTrim(command.start, command.end)
            : samples_.transaction.BeginNormalize();
    if (result == Ui2SampleEditorTransactionResult::InProgress) {
      samples_.editor.BeginApplyProgress(command.operation,
                                         TrackerAction::Enter);
      runtime_.Invalidate();
      break;
    }
    CompleteSampleEditorApply(result);
    break;
  }
  case Ui2SampleEditorCommandType::CancelApply: {
    StopSamplePreview();
    const Ui2SampleEditorTransactionResult result =
        samples_.transaction.CancelApply();
    CompleteSampleEditorApply(result);
    break;
  }
  case Ui2SampleEditorCommandType::RequestSave:
  case Ui2SampleEditorCommandType::RequestSaveAndLoad: {
    StopSamplePreview();
    std::array<char, PFILENAME_SIZE> destination{};
    const int written =
        std::snprintf(destination.data(), destination.size(), "%s",
                      samples_.transaction.DestinationPath());
    if (written <= 0 ||
        static_cast<std::size_t>(written) >= destination.size()) {
      ShowFeedbackError("SAMPLE SAVE FAILED");
      break;
    }
    const Ui2SampleEditorTransactionResult result = samples_.transaction.Save();
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
            samples_.returnPage == UiApplicationPage::Browser &&
                samples_.browser.Active(),
            [this, &destination]() {
              // Promotion recreates the directory entry. Restore the edited
              // leaf before SAVE&LOAD import or return-page rendering can
              // observe stale FAT indexes and metadata.
              (void)samples_.browser.RefreshCurrentDirectoryAndSelect(
                  destination.data());
            });
    if (followUp == Ui2SampleEditorSaveFollowUp::SaveAndLoad) {
      const char *error = nullptr;
      const bool imported =
          ImportSampleToCurrentInstrument(destination.data(), error);
      if (imported)
        (void)samples_.browser.Open(session_.ProjectName());
      (void)ActivatePage(imported ? UiApplicationPage::Browser
                                  : samples_.returnPage);
      if (!imported)
        ShowFeedbackError("SAMPLE SAVED; LOAD FAILED");
      break;
    }

    const bool projectPool = command.projectPool;
    (void)ActivatePage(samples_.returnPage);
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
        !sample->IsSliceDefined(samples_.slices.SelectedSlice())) {
      ShowFeedbackMessage("SLICE SLOT EMPTY");
      return;
    }
  }
  ExecuteSampleSlices(samples_.slices.Handle(action, pressed));
}

void Ui2TrackerApplication::HandleSampleSlicesDialog(TrackerAction action,
                                                     bool pressed) {
  ExecuteSampleSlices(samples_.slices.HandleDialog(action, pressed));
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
      if ((samples_.slices.DefinedMask() &
           static_cast<std::uint16_t>(1U << index)) != 0U)
        instrument.SetSlicePoint(index, samples_.slices.SlicePoints()[index]);
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
        note = static_cast<std::uint8_t>(std::clamp(root->GetInt(), 0, 127));
    }
    const auto instrument = static_cast<std::uint8_t>(
        std::clamp(session_.EditorState().currentInstrumentID_, 0,
                   MAX_INSTRUMENT_COUNT - 1));
    constexpr std::uint8_t previewChannel = SONG_CHANNEL_COUNT - 1U;
    player->PlayNote(instrument, previewChannel, note, 0x7FU);
    samples_.slices.StartPreview(command.start);
    samples_.preview.kind = SamplePreviewKind::SliceNote;
    samples_.preview.startedMs = System::GetInstance()->Millis();
    samples_.preview.start = command.start;
    samples_.preview.end = command.end;
    samples_.preview.frames = samples_.waveform.FrameCount();
    samples_.preview.rate = samples_.waveform.SampleRate();
    if (SamplePool *pool = SamplePool::GetInstance()) {
      if (SoundSource *source = pool->GetSource(sample->GetSampleIndex())) {
        const int rate = source->GetSampleRate(note);
        if (rate > 0)
          samples_.preview.rate = static_cast<std::uint32_t>(rate);
      }
    }
    samples_.preview.instrument = instrument;
    samples_.preview.note = note;
    // Slice preview is driven by SampleInstrument's own loop mode. The legacy
    // playhead is a one-pass duration indicator even for short waveforms.
    samples_.preview.singleCycle = false;
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
    samples_.slices.ApplyEvenSlices(command.count);
    commitSlices(*sample);
    break;
  case Ui2SampleSlicesCommandType::NavigateBack:
    ActivatePage(samples_.returnPage);
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
  samples_.slices.SynchronizeSlices(points, definedMask);
}

void Ui2TrackerApplication::StopSamplePreview() { samples_.StopPreview(); }

void Ui2TrackerApplication::UpdateSamplePreview(std::uint32_t nowMs) {
  if (samples_.preview.kind == SamplePreviewKind::None ||
      samples_.preview.rate == 0U || samples_.preview.frames == 0U)
    return;
  // A non-looping editor stream can reach EOF while PLAY is still held.
  // Legacy SampleEditorView observed Player::IsPlaying() and cleared its
  // visual state at that point; keep UI2's power state synchronized too.
  if (samples_.preview.kind == SamplePreviewKind::EditorStream) {
    Player *player = Player::GetInstance();
    if (player == nullptr || !player->IsPlaying()) {
      StopSamplePreview();
      return;
    }
  }
  const std::uint64_t elapsed = nowMs - samples_.preview.startedMs;
  const std::uint64_t advanced = elapsed * samples_.preview.rate / 1000U;
  const std::uint32_t maximum = samples_.preview.frames - 1U;
  const std::uint32_t start = std::min(samples_.preview.start, maximum);
  const std::uint32_t end = std::clamp(samples_.preview.end, start, maximum);
  const std::uint64_t span = static_cast<std::uint64_t>(end - start) + 1U;
  bool visible = true;
  std::uint32_t playhead = start;
  if (samples_.preview.singleCycle && span != 0U) {
    playhead = static_cast<std::uint32_t>(start + advanced % span);
  } else if (advanced >= span) {
    playhead = end;
    visible = false;
  } else {
    playhead = static_cast<std::uint32_t>(start + advanced);
  }
  if (samples_.preview.kind == SamplePreviewKind::EditorStream)
    samples_.editor.SetPreviewPlayhead(playhead, visible);
  else
    samples_.slices.SetPreviewPlayhead(playhead, visible);
}

} // namespace ui2
