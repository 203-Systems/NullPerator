#include "doctest/doctest.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "Application/UI2/Controllers/Ui2SampleBrowserController.h"

namespace {

class SampleBrowserFileSystem final : public FileSystem {
public:
  explicit SampleBrowserFileSystem(bool empty = false,
                                   bool rootsUnavailable = false,
                                   bool denseLibrary = false)
      : empty_(empty), rootsUnavailable_(rootsUnavailable),
        denseLibrary_(denseLibrary) {
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(this);
  }
  ~SampleBrowserFileSystem() override { FileSystem::Install(previous_); }

  FileHandle Open(const char *, const char *) override { return {}; }

  bool chdir(const char *path) override {
    if (rootsUnavailable_ &&
        (std::strcmp(path, PROJECTS_DIR) == 0 ||
         std::strcmp(path, SAMPLES_LIB_DIR) == 0))
      return false;
    if (std::strcmp(path, PROJECTS_DIR) == 0) {
      directory_ = Directory::Projects;
      return true;
    }
    if (directory_ == Directory::Projects && std::strcmp(path, "DEMO") == 0) {
      directory_ = Directory::Project;
      return true;
    }
    if (directory_ == Directory::Project &&
        std::strcmp(path, PROJECT_SAMPLES_DIR) == 0) {
      directory_ = Directory::Pool;
      return true;
    }
    if (directory_ == Directory::Pool &&
        std::strcmp(path, "NESTED") == 0) {
      ++nestedDirectoryEnterAttempts_;
      directory_ = Directory::Nested;
      return true;
    }
    if (std::strcmp(path, SAMPLES_LIB_DIR) == 0) {
      directory_ = Directory::Library;
      return true;
    }
    if (directory_ == Directory::Library &&
        std::strcmp(path, "DRUMS") == 0) {
      directory_ = Directory::Drums;
      return true;
    }
    if (std::strcmp(path, "..") == 0) {
      if (directory_ == Directory::Drums) {
        directory_ = Directory::Library;
        return true;
      }
      if (directory_ == Directory::Library) {
        directory_ = Directory::Root;
        return true;
      }
    }
    return false;
  }

  void list(etl::ivector<int> *indices, const char *filter, bool,
            bool = false) override {
    Populate(indices, filter, false);
  }

  bool listBrowserChecked(etl::ivector<int> *indices, const char *filter,
                          bool = false) override {
    Populate(indices, filter, true);
    return true;
  }

  void Populate(etl::ivector<int> *indices, const char *filter,
                bool retainDirectories) {
    indices->clear();
    if (empty_)
      return;
    if (directory_ == Directory::Pool) {
      PushIfVisible(indices, 10, filter, retainDirectories);
      PushIfVisible(indices, 20, filter,
                    retainDirectories); // .. is synthesized by adapters.
      PushIfVisible(indices, 24, filter, retainDirectories);
    } else if (directory_ == Directory::Nested) {
      PushIfVisible(indices, 20, filter, retainDirectories);
      // Deliberately duplicates the root leaf. Entering this directory used to
      // make EDIT/DELETE resolve the nested leaf against the root pool path.
      PushIfVisible(indices, 25, filter, retainDirectories);
    } else if (directory_ == Directory::Library) {
      PushIfVisible(indices, 20, filter, retainDirectories);
      PushIfVisible(indices, 21, filter, retainDirectories);
      PushIfVisible(indices, 22, filter, retainDirectories);
      if (denseLibrary_) {
        for (int index = 30; index < 42; ++index)
          PushIfVisible(indices, index, filter, retainDirectories);
      }
    } else if (directory_ == Directory::Drums) {
      PushIfVisible(indices, 20, filter, retainDirectories);
      PushIfVisible(indices, 23, filter, retainDirectories);
    }
  }

  void getFileName(int index, char *name, int length) override {
    if (name == nullptr || length <= 0)
      return;
    char generated[16]{};
    if (index >= 30 && index < 42) {
      std::snprintf(generated, sizeof(generated), "S%02d.WAV", index - 30);
    }
    const char *value = index == 10   ? "AKWF.WAV"
                        : index == 20 ? ".."
                        : index == 21 ? "KICK.WAV"
                        : index == 22 ? "DRUMS"
                        : index == 23 ? "SNARE.WAV"
                        : index == 24 ? "NESTED"
                        : index == 25 ? "AKWF.WAV"
                        : generated[0] != '\0' ? generated
                                               : "";
    std::snprintf(name, static_cast<std::size_t>(length), "%s", value);
  }

  PicoFileType getFileType(int index) override {
    if (index == 10 || index == 21 || index == 23 || index == 25 ||
        (index >= 30 && index < 42))
      return PFT_FILE;
    if (index == 20 || index == 22 || index == 24)
      return PFT_DIR;
    return PFT_UNKNOWN;
  }
  bool isParentRoot() override { return directory_ == Directory::Library; }
  bool isCurrentRoot() override { return directory_ == Directory::Root; }
  bool DeleteFile(const char *) override { return false; }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *) override { return false; }
  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int index) override {
    return index == 10 ? 13U * 1024U : index == 21 ? 1376U : 4096U;
  }
  bool CopyFile(const char *, const char *) override { return false; }
  bool MoveFile(const char *, const char *) override { return false; }
  bool isExFat() override { return false; }

  [[nodiscard]] std::uint8_t NestedDirectoryEnterAttempts() const {
    return nestedDirectoryEnterAttempts_;
  }

private:
  void PushIfVisible(etl::ivector<int> *indices, int index,
                     const char *filter, bool retainDirectories) {
    char name[PFILENAME_SIZE]{};
    getFileName(index, name, sizeof(name));
    const bool directory = getFileType(index) == PFT_DIR;
    if (std::strcmp(name, "..") != 0 && filter != nullptr && filter[0] != '\0' &&
        !(directory && retainDirectories)) {
      char lowerName[PFILENAME_SIZE]{};
      for (std::size_t i = 0U; name[i] != '\0' && i + 1U < sizeof(lowerName);
           ++i) {
        lowerName[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(name[i])));
      }
      if (std::strstr(lowerName, filter) == nullptr)
        return;
    }
    indices->push_back(index);
  }

  enum class Directory {
    Root,
    Projects,
    Project,
    Pool,
    Nested,
    Library,
    Drums
  };
  FileSystem *previous_ = nullptr;
  Directory directory_ = Directory::Root;
  std::uint8_t nestedDirectoryEnterAttempts_ = 0U;
  bool empty_ = false;
  bool rootsUnavailable_ = false;
  bool denseLibrary_ = false;
};

template <typename Controller>
auto Tap(Controller &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

} // namespace

TEST_CASE("UI2 Sample Browser opens the approved project-pool state") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  const Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  CHECK(std::strcmp(snapshot.title.data(), "SAMPLES") == 0);
  CHECK(std::strcmp(snapshot.meta.data(), "") == 0);
  CHECK(std::strcmp(snapshot.items[0].data(), "AKWF.WAV") == 0);
  CHECK(snapshot.visibleItemCount == 1U);
  CHECK(std::strcmp(snapshot.footer.data(), "13 KB  /  60") == 0);
  REQUIRE(snapshot.actionCount == 3U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "EDIT") == 0);
  CHECK(std::strcmp(snapshot.actions[1].data(), "IMPORT") == 0);
  CHECK(std::strcmp(snapshot.actions[2].data(), "DELETE") == 0);
}

TEST_CASE("UI2 Sample Browser can enter the import library directly") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.OpenLibrary("DEMO"));

  CHECK(controller.Mode() == Ui2SampleBrowserMode::Library);
  const Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  CHECK(std::strcmp(snapshot.title.data(), "IMPORT") == 0);
  CHECK(std::strcmp(snapshot.meta.data(), "") == 0);
  REQUIRE(snapshot.visibleItemCount == 2U);
  CHECK(std::strcmp(snapshot.items[0].data(), "~KICK.WAV") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "/DRUMS") == 0);
  REQUIRE(snapshot.actionCount == 3U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "IMPORT") == 0);
  CHECK(std::strcmp(snapshot.actions[2].data(), "BACK") == 0);
}

TEST_CASE("UI2 Sample Browser inherits Shift when returning from editor") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.OpenLibrary("DEMO"));

  // The editor can return to this still-open controller with SHIFT held. The
  // inherited latch changes no browser state or command by itself, but the
  // next PLAY must retain the library's SHIFT+PLAY import contract.
  controller.SetNavigationHeld(true);
  CHECK(controller.Mode() == Ui2SampleBrowserMode::Library);
  const Ui2SampleBrowserCommand import =
      controller.Handle(TrackerAction::Play, true);
  CHECK(import.type == Ui2SampleBrowserCommandType::Import);
  CHECK(std::strcmp(import.filename.data(), "KICK.WAV") == 0);
  CHECK(controller.Handle(TrackerAction::Play, false).type ==
        Ui2SampleBrowserCommandType::None);

  controller.SetNavigationHeld(false);
  CHECK(controller.Handle(TrackerAction::Play, true).type ==
        Ui2SampleBrowserCommandType::PreviewStart);
  CHECK(controller.Handle(TrackerAction::Play, false).type ==
        Ui2SampleBrowserCommandType::PreviewStop);
}

TEST_CASE("UI2 Sample Browser keeps project pool flat and root-addressed") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  const Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.visibleItemCount == 1U);
  CHECK(std::strcmp(snapshot.items[0].data(), "AKWF.WAV") == 0);

  // Trying to move to the hidden NESTED directory cannot change selection or
  // enter it. Therefore neither action can emit the same-named nested leaf.
  Tap(controller, TrackerAction::Down);
  const Ui2SampleBrowserCommand edit = Tap(controller, TrackerAction::Edit);
  CHECK(edit.type == Ui2SampleBrowserCommandType::Edit);
  CHECK(edit.projectSample);
  CHECK(std::strcmp(edit.filename.data(), "AKWF.WAV") == 0);

  Tap(controller, TrackerAction::Left); // DELETE wraps from EDIT.
  const Ui2SampleBrowserCommand remove = Tap(controller, TrackerAction::Edit);
  CHECK(remove.type == Ui2SampleBrowserCommandType::RequestDelete);
  CHECK(remove.projectSample);
  CHECK(std::strcmp(remove.filename.data(), "AKWF.WAV") == 0);
  CHECK(fileSystem.NestedDirectoryEnterAttempts() == 0U);
}

TEST_CASE("UI2 Sample Browser library retains directories and parent chord") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  controller.Handle(TrackerAction::Shift, true);
  REQUIRE(controller.Handle(TrackerAction::Option, true).type ==
          Ui2SampleBrowserCommandType::ModeChanged);
  controller.Handle(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Shift, false);
  REQUIRE(controller.Mode() == Ui2SampleBrowserMode::Library);

  Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.visibleItemCount == 2U);
  CHECK(std::strcmp(snapshot.items[0].data(), "~KICK.WAV") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "/DRUMS") == 0);
  REQUIRE(snapshot.actionCount == 3U);
  CHECK(std::strcmp(snapshot.actions[2].data(), "BACK") == 0);

  Tap(controller, TrackerAction::Down);
  snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.actionCount == 2U);
  CHECK(std::strcmp(snapshot.actions[1].data(), "BACK") == 0);
  CHECK_FALSE(Tap(controller, TrackerAction::Edit).HasValue());
  snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.visibleItemCount == 2U);
  CHECK(std::strcmp(snapshot.items[0].data(), "..") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "SNARE.WAV") == 0);

  controller.Handle(TrackerAction::Option, true);
  CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
  controller.Handle(TrackerAction::Left, false);
  controller.Handle(TrackerAction::Option, false);
  snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.visibleItemCount == 2U);
  CHECK(std::strcmp(snapshot.items[1].data(), "/DRUMS") == 0);
}

TEST_CASE("UI2 Sample Browser directory BACK action is reachable") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.OpenLibrary("DEMO"));

  Tap(controller, TrackerAction::Down); // DRUMS directory
  Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.actionCount == 2U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "OPEN") == 0);
  CHECK(std::strcmp(snapshot.actions[1].data(), "BACK") == 0);

  Tap(controller, TrackerAction::Right);
  snapshot = controller.Snapshot(60);
  CHECK(snapshot.activeAction == 1U);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2SampleBrowserCommandType::Back);
}

TEST_CASE("UI2 Sample Browser parent chord cannot escape the library root") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.OpenLibrary("DEMO"));

  controller.Handle(TrackerAction::Option, true);
  CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
  controller.Handle(TrackerAction::Left, false);
  controller.Handle(TrackerAction::Option, false);

  const Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  REQUIRE(snapshot.visibleItemCount == 2U);
  CHECK(std::strcmp(snapshot.items[0].data(), "~KICK.WAV") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "/DRUMS") == 0);
}

TEST_CASE("UI2 Sample Browser previews, imports, and restores pool mode") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  const Ui2SampleBrowserCommand preview =
      controller.Handle(TrackerAction::Play, true);
  CHECK(preview.type == Ui2SampleBrowserCommandType::PreviewStart);
  CHECK(std::strcmp(preview.filename.data(), "AKWF.WAV") == 0);
  CHECK(controller.Handle(TrackerAction::Play, false).type ==
        Ui2SampleBrowserCommandType::PreviewStop);

  Tap(controller, TrackerAction::Right); // approved IMPORT action
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2SampleBrowserCommandType::ModeChanged);
  CHECK(controller.Mode() == Ui2SampleBrowserMode::Library);
  CHECK(std::strcmp(controller.Snapshot(40).items[0].data(),
                    "~KICK.WAV") == 0);

  const Ui2SampleBrowserCommand import =
      Tap(controller, TrackerAction::Edit);
  CHECK(import.type == Ui2SampleBrowserCommandType::Import);
  CHECK(std::strcmp(import.filename.data(), "KICK.WAV") == 0);

  controller.Handle(TrackerAction::Shift, true);
  CHECK(controller.Handle(TrackerAction::Option, true).type ==
        Ui2SampleBrowserCommandType::ModeChanged);
  CHECK(controller.Mode() == Ui2SampleBrowserMode::ProjectPool);
  controller.Handle(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Shift, false);
}

TEST_CASE("UI2 Sample Browser accepts held-direction repeat pulses") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  controller.Handle(TrackerAction::Right, true);
  controller.Handle(TrackerAction::Right, true);
  controller.Handle(TrackerAction::Right, false);

  const Ui2BrowserSnapshot snapshot = controller.Snapshot(40);
  REQUIRE(snapshot.actionCount == 3U);
  CHECK(snapshot.activeAction == 2U);
  CHECK(std::strcmp(snapshot.actions[2].data(), "DELETE") == 0);
}

TEST_CASE("UI2 Sample Browser option directions jump eight entries") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem(false, false, true);
  Ui2SampleBrowserController controller;
  REQUIRE(controller.OpenLibrary("DEMO"));

  controller.Handle(TrackerAction::Option, true);
  Tap(controller, TrackerAction::Down);
  Ui2BrowserSnapshot snapshot = controller.Snapshot(40);
  CHECK(snapshot.selectedRow == 8U);
  CHECK(std::strcmp(snapshot.items[snapshot.selectedRow - snapshot.topIndex]
                        .data(),
                    "S06.WAV") == 0);

  Tap(controller, TrackerAction::Up);
  snapshot = controller.Snapshot(40);
  CHECK(snapshot.selectedRow == 0U);
  CHECK(std::strcmp(snapshot.items[0].data(), "~KICK.WAV") == 0);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Sample Browser option edit requests confirmed pool deletion") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  controller.Handle(TrackerAction::Option, true);
  const Ui2SampleBrowserCommand request =
      controller.Handle(TrackerAction::Edit, true);
  REQUIRE(request.type == Ui2SampleBrowserCommandType::RequestDelete);
  CHECK(request.projectSample);
  CHECK(std::strcmp(request.filename.data(), "AKWF.WAV") == 0);
  controller.Handle(TrackerAction::Edit, false);
  controller.Handle(TrackerAction::Option, false);

  REQUIRE(controller.OpenLibrary("DEMO"));
  controller.Handle(TrackerAction::Option, true);
  CHECK_FALSE(controller.Handle(TrackerAction::Edit, true).HasValue());
}

TEST_CASE("UI2 Sample Browser exposes actions for empty states") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem(true);
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  CHECK_FALSE(snapshot.hasSelection);
  REQUIRE(snapshot.actionCount == 1U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "IMPORT") == 0);
  REQUIRE(Tap(controller, TrackerAction::Edit).type ==
          Ui2SampleBrowserCommandType::ModeChanged);

  snapshot = controller.Snapshot(60);
  CHECK_FALSE(snapshot.hasSelection);
  REQUIRE(snapshot.actionCount == 1U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "BACK") == 0);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2SampleBrowserCommandType::Back);
}

TEST_CASE("UI2 Sample Browser exposes root failures as exit-capable states") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem(false, true);

  Ui2SampleBrowserController pool;
  REQUIRE(pool.Open("DEMO"));
  CHECK(pool.Active());
  Ui2BrowserSnapshot snapshot = pool.Snapshot(60);
  CHECK_FALSE(snapshot.hasSelection);
  CHECK(std::strcmp(snapshot.footer.data(), "SAMPLE POOL UNAVAILABLE") == 0);
  REQUIRE(snapshot.actionCount == 1U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "BACK") == 0);
  CHECK(Tap(pool, TrackerAction::Edit).type ==
        Ui2SampleBrowserCommandType::Back);

  Ui2SampleBrowserController library;
  REQUIRE(library.OpenLibrary("DEMO"));
  CHECK(library.Active());
  snapshot = library.Snapshot(60);
  CHECK_FALSE(snapshot.hasSelection);
  CHECK(std::strcmp(snapshot.footer.data(), "SAMPLE LIB UNAVAILABLE") == 0);
  REQUIRE(snapshot.actionCount == 1U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "BACK") == 0);
  CHECK(Tap(library, TrackerAction::Edit).type ==
        Ui2SampleBrowserCommandType::Back);

  Ui2SampleBrowserController invalid;
  CHECK_FALSE(invalid.Open(""));
  CHECK_FALSE(invalid.Active());
}

TEST_CASE("UI2 Sample Browser delete confirmation defaults to NO") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));
  Tap(controller, TrackerAction::Left); // DELETE wraps from EDIT
  const Ui2SampleBrowserCommand request = Tap(controller, TrackerAction::Edit);
  REQUIRE(request.type == Ui2SampleBrowserCommandType::RequestDelete);
  controller.RequestDeleteConfirmation(request.filename.data());
  REQUIRE(controller.DialogActive());
  const Ui2DialogSnapshot dialog = controller.DialogSnapshot();
  CHECK(dialog.selectedAction == 1U);
  CHECK(std::string_view(dialog.label.data()) == "AKWF.WAV");
  CHECK(dialog.labelUserText);
  CHECK(controller.HandleDialog(TrackerAction::Edit, true).type ==
        Ui2SampleBrowserCommandType::None);
  controller.HandleDialog(TrackerAction::Edit, false);
  CHECK_FALSE(controller.DialogActive());

  controller.RequestDeleteConfirmation("AKWF.WAV");
  controller.HandleDialog(TrackerAction::Left, true);
  controller.HandleDialog(TrackerAction::Left, false);
  const Ui2SampleBrowserCommand confirmed =
      controller.HandleDialog(TrackerAction::Edit, true);
  CHECK(confirmed.type == Ui2SampleBrowserCommandType::DeleteConfirmed);
  CHECK(std::strcmp(confirmed.filename.data(), "AKWF.WAV") == 0);
}

TEST_CASE("UI2 Sample Browser delete dialog waits for its EDIT release") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));
  Tap(controller, TrackerAction::Left); // DELETE wraps from EDIT

  // Keep the activating EDIT physically held while the dialog opens.
  const Ui2SampleBrowserCommand request =
      controller.Handle(TrackerAction::Edit, true);
  REQUIRE(request.type == Ui2SampleBrowserCommandType::RequestDelete);
  controller.RequestDeleteConfirmation(request.filename.data(),
                                       TrackerAction::Edit);
  REQUIRE(controller.DialogActive());

  CHECK(controller.HandleDialog(TrackerAction::Edit, true).type ==
        Ui2SampleBrowserCommandType::None);
  CHECK(controller.HandleDialog(TrackerAction::Left, true).type ==
        Ui2SampleBrowserCommandType::None);
  CHECK(controller.HandleDialog(TrackerAction::Left, false).type ==
        Ui2SampleBrowserCommandType::None);
  CHECK(controller.DialogActive());
  CHECK(controller.DialogSnapshot().selectedAction == 1U);
  CHECK(controller.HandleDialog(TrackerAction::Edit, false).type ==
        Ui2SampleBrowserCommandType::None);

  CHECK(controller.HandleDialog(TrackerAction::Left, true).type ==
        Ui2SampleBrowserCommandType::None);
  CHECK(controller.HandleDialog(TrackerAction::Left, false).type ==
        Ui2SampleBrowserCommandType::None);
  const Ui2SampleBrowserCommand confirmed =
      controller.HandleDialog(TrackerAction::Edit, true);
  CHECK(confirmed.type == Ui2SampleBrowserCommandType::DeleteConfirmed);
  CHECK(std::strcmp(confirmed.filename.data(), "AKWF.WAV") == 0);
}

TEST_CASE("UI2 Sample Browser clears modifier releases owned by its dialog") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO"));

  controller.Handle(TrackerAction::Option, true);
  const Ui2SampleBrowserCommand request =
      controller.Handle(TrackerAction::Edit, true);
  REQUIRE(request.type == Ui2SampleBrowserCommandType::RequestDelete);
  controller.RequestDeleteConfirmation(request.filename.data(),
                                       TrackerAction::Edit);

  // Mirror Ui2TrackerApplication's modal release routing: the dialog consumes
  // the release first, then the browser press owner receives the same key-up.
  controller.HandleDialog(TrackerAction::Edit, false);
  controller.Handle(TrackerAction::Edit, false);
  controller.HandleDialog(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Option, false);

  controller.HandleDialog(TrackerAction::Left, true); // YES
  controller.HandleDialog(TrackerAction::Left, false);
  REQUIRE(controller.HandleDialog(TrackerAction::Edit, true).type ==
          Ui2SampleBrowserCommandType::DeleteConfirmed);
  controller.Handle(TrackerAction::Edit, false);

  // OPTION is no longer latched: ordinary EDIT opens the sample editor rather
  // than immediately requesting another delete.
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2SampleBrowserCommandType::Edit);
}
