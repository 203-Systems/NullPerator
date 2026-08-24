/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "Application/UI2/Ui2TrackerApplication.h"

#include "Application/Model/Config.h"
#include "Application/Model/Groove.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/Player.h"
#include "Services/Audio/Audio.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"

#include <cstring>
#include <algorithm>

namespace ui2 {
namespace {

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
  case UiApplicationPage::Browser:
  case UiApplicationPage::SampleEditor:
  case UiApplicationPage::SampleSlices:
    return Ui2TrackerPage::None;
  }
  return Ui2TrackerPage::None;
}

} // namespace

Ui2TrackerApplication::Ui2TrackerApplication(IUiPresenter &presenter)
    : session_(UNNAMED_PROJECT_NAME), modelPort_(session_),
      tracker_(modelPort_), source_(session_, tracker_, project_, groove_),
      runtime_(presenter) {}

bool Ui2TrackerApplication::Init() {
  PersistencyService::GetInstance();
  FileSystem *fileSystem = FileSystem::GetInstance();
  EnsureDirectory(fileSystem, PROJECTS_DIR);
  EnsureDirectory(fileSystem, SAMPLES_LIB_DIR);
  EnsureDirectory(fileSystem, INSTRUMENTS_DIR);
  EnsureDirectory(fileSystem, RENDERS_DIR);
  EnsureDirectory(fileSystem, THEMES_DIR);
  EnsureDirectory(fileSystem, RECORDINGS_DIR);

  char projectName[MAX_PROJECT_NAME_LENGTH + 1U]{};
  bool createProject = false;
  if (PersistencyService::GetInstance()->LoadCurrentProjectName(projectName) !=
      PERSIST_LOADED) {
    std::strncpy(projectName, UNNAMED_PROJECT_NAME, sizeof(projectName) - 1U);
    createProject = true;
  }
  if (session_.LoadProject(projectName, createProject) !=
      TrackerApplicationSession::LoadResult::Loaded) {
    if (std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0 ||
        session_.LoadProject(UNNAMED_PROJECT_NAME, true) !=
            TrackerApplicationSession::LoadResult::Loaded)
      return false;
  }

  Audio::GetInstance()->Init();
  if (Config *config = Config::GetInstance()) {
    if (Variable *brightness =
            config->FindVariable(FourCC::VarBacklightLevel)) {
      System::GetInstance()->SetDisplayBrightness(
          static_cast<unsigned char>(brightness->GetInt()));
    }
  }

  initialized_ = true;
  ActivatePage(UiApplicationPage::Song);
  return true;
}

void Ui2TrackerApplication::DispatchTrackerAction(TrackerAction action,
                                                  bool pressed) {
  if (!initialized_ || action >= TrackerAction::Count)
    return;

  switch (activePage_) {
  case UiApplicationPage::Song:
  case UiApplicationPage::Chain:
  case UiApplicationPage::Phrase:
  case UiApplicationPage::Table:
    tracker_.Handle(action, pressed);
    SynchronizeGridPage();
    break;
  case UiApplicationPage::Project:
    HandleProject(action, pressed);
    break;
  case UiApplicationPage::Groove:
    HandleGroove(action, pressed);
    break;
  case UiApplicationPage::Device:
    if (pressed && action == TrackerAction::Down)
      ActivatePage(UiApplicationPage::Project);
    break;
  case UiApplicationPage::Mixer:
    if (pressed && action == TrackerAction::Up)
      ActivatePage(UiApplicationPage::Song);
    break;
  case UiApplicationPage::Instrument:
  case UiApplicationPage::Theme:
  case UiApplicationPage::Browser:
  case UiApplicationPage::SampleEditor:
  case UiApplicationPage::SampleSlices:
  case UiApplicationPage::Record:
  case UiApplicationPage::None:
    break;
  }
}

PresentResult Ui2TrackerApplication::Present() {
  return initialized_ ? runtime_.Present(source_) : PresentResult::Failed;
}

bool Ui2TrackerApplication::ActivatePage(UiApplicationPage page) {
  if (page == UiApplicationPage::None)
    return false;
  const bool changed = activePage_ != page;
  activePage_ = page;
  source_.SetActivePage(page);
  const Ui2TrackerPage trackerPage = TrackerPageFor(page);
  if (trackerPage != Ui2TrackerPage::None) {
    tracker_.Hub().Activate(trackerPage);
    modelPort_.StoreGridNavigation(tracker_.Hub().Navigation());
  }
  if (changed)
    runtime_.Invalidate();
  return changed;
}

void Ui2TrackerApplication::HandleProject(TrackerAction action,
                                         bool pressed) {
  if (!projectInput_.Update(action, pressed) || !pressed)
    return;

  if (projectInput_.Held(TrackerAction::Nav)) {
    if (action == TrackerAction::Down)
      ActivatePage(UiApplicationPage::Song);
    else if (action == TrackerAction::Up)
      ActivatePage(UiApplicationPage::Device);
    return;
  }
  if (projectInput_.Held(TrackerAction::Edit)) {
    if (action == TrackerAction::Play)
      ActivatePage(UiApplicationPage::Record);
    return;
  }
  if (projectInput_.AnyModifier())
    return;

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
  case TrackerAction::Enter:
    ExecuteProject(project_.Enter());
    break;
  case TrackerAction::Play:
    Player::GetInstance()->OnStartButton(PM_SONG,
                                         session_.EditorState().songX_, false,
                                         session_.EditorState().songX_);
    break;
  case TrackerAction::Alt:
  case TrackerAction::Edit:
  case TrackerAction::Nav:
  case TrackerAction::Select:
  case TrackerAction::Power:
  case TrackerAction::Count:
    break;
  }
}

void Ui2TrackerApplication::HandleGroove(TrackerAction action, bool pressed) {
  ExecuteGroove(groove_.Handle(action, pressed));
}

void Ui2TrackerApplication::ExecuteProject(Ui2ProjectCommand command) {
  switch (command.type) {
  case Ui2ProjectCommandType::SaveProject:
    PersistencyService::GetInstance()->Save(session_.ProjectName(),
                                            session_.ProjectName(), false);
    PersistencyService::GetInstance()->SaveProjectState(session_.ProjectName());
    break;
  case Ui2ProjectCommandType::RemoveUnusedSamples:
    session_.ProjectModel().PurgeSamples();
    break;
  case Ui2ProjectCommandType::RemoveUnusedInstruments:
    session_.ProjectModel().PurgeInstruments();
    break;
  case Ui2ProjectCommandType::NewProject:
  case Ui2ProjectCommandType::LoadProject:
  case Ui2ProjectCommandType::RenameProject:
  case Ui2ProjectCommandType::BrowseSamplePool:
  case Ui2ProjectCommandType::RenderMixdown:
  case Ui2ProjectCommandType::RenderStems:
  case Ui2ProjectCommandType::None:
    // These commands require a dedicated UI2 browser, rename page, or render
    // progress state. They remain typed here rather than falling through to a
    // legacy View implementation.
    break;
  }
}

void Ui2TrackerApplication::ExecuteGroove(Ui2GrooveCommand command) {
  if (!command.HasValue())
    return;
  const std::uint8_t number = groove_.Number();
  std::uint8_t *steps = Groove::GetInstance()->GetGrooveData(number);
  switch (command.type) {
  case Ui2GrooveCommandType::InitializeStep:
    steps[command.row] = 6U;
    break;
  case Ui2GrooveCommandType::ClearStep:
    steps[command.row] = 0xFFU;
    break;
  case Ui2GrooveCommandType::AdjustStep: {
    std::uint8_t &step = steps[command.row];
    const int current = step == 0xFFU ? 0 : step;
    step = static_cast<std::uint8_t>(
        std::clamp(current + command.value, 0, 0xFF));
    break;
  }
  case Ui2GrooveCommandType::SelectNumber:
    session_.EditorState().currentGroove_ = groove_.Number();
    break;
  case Ui2GrooveCommandType::StartPlayback:
    Player::GetInstance()->OnStartButton(PM_SONG,
                                         session_.EditorState().songX_, false,
                                         session_.EditorState().songX_);
    break;
  case Ui2GrooveCommandType::OpenRecord:
    ActivatePage(UiApplicationPage::Record);
    break;
  case Ui2GrooveCommandType::None:
    break;
  }
}

void Ui2TrackerApplication::SynchronizeGridPage() {
  const UiApplicationPage page = tracker_.ActiveApplicationPage();
  if (page != UiApplicationPage::None && page != activePage_)
    ActivatePage(page);
}

} // namespace ui2
