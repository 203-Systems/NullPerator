/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "SelectProjectView.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "BaseClasses/ViewEvent.h"

#include <algorithm>
#include <cstring>
#include <nanoprintf.h>

#define LIST_PAGE_SIZE (SCREEN_HEIGHT - 4)
#define LIST_WIDTH 26
#define INVALID_PROJECT_NAME "INVALID NAME"

static void LoadProjectCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {

    // User accepted losing changes; clear autosave for the current project.
    ((SelectProjectView &)v).ClearAutoSave();

    ((SelectProjectView &)v).LoadProject();
  }
}

static void DeleteProjectCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    SelectProjectView &view = (SelectProjectView &)v;

    PersistencyService *ps = PersistencyService::GetInstance();
    char buffer[MAX_PROJECT_NAME_LENGTH + 1];
    view.getHighlightedProjectName(buffer);
    if (!ps->DeleteProject(buffer)) {
      MessageBox *mb =
          MessageBox::Create(view, "Project could not be deleted", MBBF_OK);
      view.DoModal(mb);
      return;
    }

    // reload list
    view.setCurrentFolder();
  }
}

SelectProjectView::SelectProjectView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData) {}

SelectProjectView::~SelectProjectView() {}

Ui2BrowserSnapshot SelectProjectView::SnapshotForUi2() const {
  Ui2BrowserSnapshot snapshot;
  Ui2BrowserSnapshot::CopyText(snapshot.title, "BROWSE");
  snapshot.ConfigureWindow(fileIndexList_.size(), currentIndex_, topIndex_);

  char currentProject[MAX_PROJECT_NAME_LENGTH + 1]{};
  if (viewData_ != nullptr && viewData_->project_ != nullptr) {
    if (Variable *name =
            viewData_->project_->FindVariable(FourCC::VarProjectName)) {
      const auto value = name->GetString();
      npf_snprintf(currentProject, sizeof(currentProject), "%s",
                   value.c_str());
    }
  }

  FileSystem *fs = FileSystem::GetInstance();
  bool selectedNameIsValid = false;
  bool selectedIsCurrentProject = false;
  if (fs != nullptr) {
    for (std::uint8_t row = 0; row < snapshot.visibleItemCount; ++row) {
      const std::size_t listIndex = snapshot.topIndex + row;
      const int fileIndex = fileIndexList_[listIndex];
      char filename[MAX_PROJECT_NAME_LENGTH + 1]{};
      if (fs->getFileType(fileIndex) == PFT_DIR)
        fs->getFileName(fileIndex, filename, sizeof(filename));
      filename[sizeof(filename) - 1U] = '\0';
      const bool validName = filename[0] != '\0';
      if (!validName)
        std::strcpy(filename, INVALID_PROJECT_NAME);

      if (row == snapshot.selectedRow) {
        selectedNameIsValid = validName;
        selectedIsCurrentProject =
            validName && std::strcmp(filename, currentProject) == 0;
      }

      char display[MAX_PROJECT_NAME_LENGTH + 2U]{};
      npf_snprintf(display, sizeof(display), "%s%s",
                   std::strcmp(filename, currentProject) == 0 ? "*" : "",
                   filename);
      Ui2BrowserSnapshot::CopyText(snapshot.items[row], display);
    }
  }

  npf_snprintf(snapshot.footer.data(), snapshot.footer.size(), "%u ITEM%s",
               static_cast<unsigned int>(snapshot.totalItemCount),
               snapshot.totalItemCount == 1U ? "" : "S");
  Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "LOAD");
  Ui2BrowserSnapshot::CopyText(snapshot.actions[1], "DELETE");
  if (snapshot.hasSelection && selectedNameIsValid) {
    // The active project cannot be deleted. Do not advertise an action that
    // the controller will reject with a modal.
    snapshot.actionCount = selectedIsCurrentProject ? 1U : numButtons_;
    snapshot.activeAction =
        snapshot.actionCount == 1U
            ? 0U
            : static_cast<std::uint8_t>(
                  std::clamp(selectedButton_, 0, numButtons_ - 1));
  }
  return snapshot;
}

void SelectProjectView::Reset() {
  topIndex_ = 0;
  currentIndex_ = 0;
  selectedButton_ = 0;
  selection_[0] = '\0';
  fileIndexList_.clear();
}

void SelectProjectView::DrawView() {
  Clear();

  GUITextProperties props;
  GUIPoint pos = GetTitlePosition();
  SetColor(CD_NORMAL);

  auto fs = FileSystem::GetInstance();

  // Draw title
  const char *title = "Browse Projects";
  SetColor(CD_INFO);
  DrawString(pos._x, pos._y, title, props);
  SetColor(CD_NORMAL);

  // Draw projects
  int x = 2;
  int y = pos._y + 2;

  auto var = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = var->GetString();
  const char *currentProject = projectName.c_str();
  size_t total = fileIndexList_.size();

  for (size_t i = topIndex_; i < topIndex_ + LIST_PAGE_SIZE && (i < total);
       i++) {
    if (i == currentIndex_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }

    char buffer[MAX_PROJECT_NAME_LENGTH + 1];
    memset(buffer, '\0', sizeof(buffer));
    unsigned fileIndex = fileIndexList_[i];

    if (fs->getFileType(fileIndex) == PFT_DIR) {
      fs->getFileName(fileIndex, buffer, MAX_PROJECT_NAME_LENGTH + 1);
    }
    // SDFat lib doesn't truncate if filename longer than buffer as per docs but
    // instead returns empty string in buffer
    if (strlen(buffer) == 0) {
      strcpy(buffer, INVALID_PROJECT_NAME);
    }

    if (strcmp(buffer, currentProject) == 0) {
      // mark currently loaded project
      DrawString(x - 1, y, "*", props);
    }

    DrawString(x, y, buffer, props);
    y += 1;
  };

  // load/delete selection buttons
  const char *buttons[numButtons_] = {"Load", "Delete"};

  int bx = x;

  for (int n = 0; n < numButtons_; n++) {
    bool selected = selectedButton_ == n;
    props.invert_ = selected;
    SetColor(selected ? CD_HILITE2 : CD_HILITE1);
    DrawString(x, SCREEN_HEIGHT - 1, buttons[n], props);

    x += 2 + strlen(buttons[n]);
  }

  // scroll bar
  drawScrollBar(SCREEN_WIDTH - 1, pos._y + 2, LIST_PAGE_SIZE, topIndex_, total);
};

void SelectProjectView::OnPlayerUpdate(PlayerEventType,
                                       unsigned int currentTick){};

void SelectProjectView::OnFocus() {
  selectedButton_ = 0; // Always default to "Load" when entering this view.
  setCurrentFolder();
};

void SelectProjectView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed)
    return;

  if (mask & EPBM_EDIT) {
    // EDIT+ENTER -> hotkey to delete
    if (mask & EPBM_ENTER)
      AttemptDeletingSelectedProject();
    if (mask & EPBM_UP)
      warpToNextProject(true);
    if (mask & EPBM_DOWN)
      warpToNextProject(false);
  } else {
    // A modifier
    if (mask & EPBM_ENTER) {
      switch (selectedButton_) {
      case 0:
        // load project
        AttemptLoadingProject();
        break;
      case 1:
        AttemptDeletingSelectedProject();
        break;
      }
    } else {
      // R Modifier
      if ((mask & EPBM_NAV) && (mask & EPBM_LEFT)) {
        // Go to back "left" to Project Screen
        ViewType vt = VT_PROJECT;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        SetChanged();
        NotifyObservers(&ve);
        return;
      } else {
        // No modifier
        if (mask == EPBM_UP)
          warpToNextProject(true);
        if (mask == EPBM_DOWN)
          warpToNextProject(false);
        if (mask == EPBM_LEFT)
          SelectButton(-1);
        if (mask == EPBM_RIGHT)
          SelectButton(1);
      }
    }
  }
}

void SelectProjectView::warpToNextProject(bool goUp) {

  if (fileIndexList_.empty()) {
    return;
  }

  const size_t previousIndex = currentIndex_;

  if (goUp) {
    if (currentIndex_ > 0) {
      currentIndex_--;
      // if we have scrolled off the top, page the file list up if not
      // already at  very top of the list
      if (currentIndex_ < topIndex_) {
        topIndex_ = currentIndex_;
      }
    }
  } else {
    if (currentIndex_ < fileIndexList_.size() - 1) {
      currentIndex_++;
      // if we have scrolled off the bottom, page the file list down if not
      // at end of the list
      if (currentIndex_ >= (topIndex_ + LIST_PAGE_SIZE)) {
        topIndex_++;
      }
    }
  }
  if (currentIndex_ != previousIndex) {
    // Each row starts on its primary action. This also prevents a hidden
    // DELETE selection carrying onto the active project row.
    selectedButton_ = 0;
  }
  isDirty_ = true;
}

void SelectProjectView::setCurrentFolder() {
  auto fs = FileSystem::GetInstance();
  fs->chdir(PROJECTS_DIR);

  // get ready
  fileIndexList_.clear();

  // Let's read all the directory in the project dir
  fs->list(&fileIndexList_, "", true);

  // Filter out "." and ".." along with the hidden default project entry
  for (auto it = fileIndexList_.begin(); it != fileIndexList_.end();) {
    fs->getFileName(*it, selection_, MAX_PROJECT_NAME_LENGTH + 1);

    const bool isDotEntry =
        (strcmp(selection_, ".") == 0) || (strcmp(selection_, "..") == 0);
    const bool isUntitled = (strcmp(selection_, UNNAMED_PROJECT_NAME) == 0);

    if (isDotEntry || isUntitled) {
      if (isUntitled) {
        Trace::Log("SELECTPROJECTVIEW", "skipping untitled project on Index:%d",
                   static_cast<int>(it - fileIndexList_.begin()));
      }
      it = fileIndexList_.erase(it);
    } else {
      ++it;
    }
  }

  // reset & redraw screen
  topIndex_ = 0;
  currentIndex_ = 0;
  selectedButton_ = 0;
  isDirty_ = true;
}

void SelectProjectView::getSelectedProjectName(char *name) {
  strcpy(name, selection_);
}

void SelectProjectView::getHighlightedProjectName(char *name) {
  name[0] = '\0';
  if (currentIndex_ >= fileIndexList_.size()) {
    return;
  }

  auto fs = FileSystem::GetInstance();
  unsigned fileIndex = fileIndexList_[currentIndex_];
  fs->getFileName(fileIndex, name, MAX_PROJECT_NAME_LENGTH + 1);
}

void SelectProjectView::SelectButton(int direction) {
  if (!HasActionableSelection() || SelectionIsCurrentProject()) {
    selectedButton_ = 0;
    return;
  }
  selectedButton_ = (numButtons_ + selectedButton_ + direction) % numButtons_;
  isDirty_ = true;
}

void SelectProjectView::LoadProject() {
  if (currentIndex_ >= fileIndexList_.size()) {
    return;
  }

  // all subdirs directly inside /project are expected to be projects
  unsigned fileIndex = fileIndexList_[currentIndex_];
  auto fs = FileSystem::GetInstance();
  fs->getFileName(fileIndex, selection_, MAX_PROJECT_NAME_LENGTH + 1);
  if (strlen(selection_) == 0) {
    Trace::Log("SELECTPROJECTVIEW",
               "skipping too long project name on Index:%d", fileIndex);
    return;
  }

  Trace::Log("SELECTPROJECTVIEW", "Select Project:%s", selection_);

  ViewEvent ve(VET_LOAD_PROJECT, selection_);
  SetChanged();
  NotifyObservers(&ve);
}

bool SelectProjectView::WarnPlayerRunning() {
  if (Player::GetInstance()->IsRunning()) {
    MessageBox *mb = MessageBox::Create(*this, "Not while running!", MBBF_OK);
    DoModal(mb);
    return true;
  }
  return false;
}

bool SelectProjectView::HasActionableSelection() {
  if (currentIndex_ >= fileIndexList_.size()) {
    return false;
  }
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr) {
    return false;
  }
  const unsigned fileIndex = fileIndexList_[currentIndex_];
  if (fs->getFileType(fileIndex) != PFT_DIR) {
    return false;
  }
  char selected[MAX_PROJECT_NAME_LENGTH + 1]{};
  fs->getFileName(fileIndex, selected, sizeof(selected));
  selected[sizeof(selected) - 1U] = '\0';
  return selected[0] != '\0';
}

bool SelectProjectView::SelectionIsCurrentProject() {
  if (!HasActionableSelection()) {
    return false;
  }

  char selected[MAX_PROJECT_NAME_LENGTH + 1];
  getHighlightedProjectName(selected);

  auto var = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = var->GetString();
  const char *current = projectName.c_str();

  return strcmp(current, selected) == 0;
}

void SelectProjectView::AttemptDeletingSelectedProject() {
  if (!HasActionableSelection()) {
    return;
  }

  if (WarnPlayerRunning()) {
    return;
  }

  if (SelectionIsCurrentProject()) {
    MessageBox *mb = MessageBox::Create(*this, "Cannot delete the active",
                                        "project.", MBBF_OK);
    DoModal(mb);
    return;
  }

  char selected[MAX_PROJECT_NAME_LENGTH + 1];
  getHighlightedProjectName(selected);

  MessageBox *mb = MessageBox::Create(*this, "Delete selected project?",
                                      selected, MBBF_YES | MBBF_NO);
  DoModal(mb, ModalViewCallback::create<&DeleteProjectCallback>());
}

void SelectProjectView::AttemptLoadingProject() {
  if (!HasActionableSelection()) {
    return;
  }

  if (WarnPlayerRunning()) {
    return;
  }

  MessageBox *mb = MessageBox::Create(*this, "Load song and lose changes?",
                                      MBBF_YES | MBBF_NO);
  DoModal(mb, ModalViewCallback::create<&LoadProjectCallback>());
}

void SelectProjectView::ClearAutoSave() {
  auto var = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = var->GetString();
  PersistencyService *ps = PersistencyService::GetInstance();
  if (!projectName.empty()) {
    if (!ps->ClearAutosave(projectName.c_str())) {
      Trace::Log("SELECTPROJECTVIEW",
                 "Autosave clear failed or missing for project: %s",
                 projectName.c_str());
    }
  }
}
