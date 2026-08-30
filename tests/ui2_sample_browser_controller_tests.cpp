#include "doctest/doctest.h"

#include <cstdio>
#include <cstring>

#include "Application/UI2/Controllers/Ui2SampleBrowserController.h"

namespace {

class SampleBrowserFileSystem final : public FileSystem {
public:
  explicit SampleBrowserFileSystem(bool empty = false) : empty_(empty) {
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(this);
  }
  ~SampleBrowserFileSystem() override { FileSystem::Install(previous_); }

  FileHandle Open(const char *, const char *) override { return {}; }

  bool chdir(const char *path) override {
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

  void list(etl::ivector<int> *indices, const char *, bool,
            bool = false) override {
    indices->clear();
    if (empty_)
      return;
    if (directory_ == Directory::Pool) {
      indices->push_back(10);
      indices->push_back(20); // .. must not be exposed by ProjectPool.
      indices->push_back(24); // Nor may a real nested directory be exposed.
    } else if (directory_ == Directory::Nested) {
      indices->push_back(20);
      // Deliberately duplicates the root leaf. Entering this directory used to
      // make EDIT/DELETE resolve the nested leaf against the root pool path.
      indices->push_back(25);
    } else if (directory_ == Directory::Library) {
      indices->push_back(20);
      indices->push_back(21);
      indices->push_back(22);
    } else if (directory_ == Directory::Drums) {
      indices->push_back(20);
      indices->push_back(23);
    }
  }

  void getFileName(int index, char *name, int length) override {
    if (name == nullptr || length <= 0)
      return;
    const char *value = index == 10   ? "AKWF.WAV"
                        : index == 20 ? ".."
                        : index == 21 ? "KICK.WAV"
                        : index == 22 ? "DRUMS"
                        : index == 23 ? "SNARE.WAV"
                        : index == 24 ? "NESTED"
                        : index == 25 ? "AKWF.WAV"
                                      : "";
    std::snprintf(name, static_cast<std::size_t>(length), "%s", value);
  }

  PicoFileType getFileType(int index) override {
    if (index == 10 || index == 21 || index == 23 || index == 25)
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
  REQUIRE(controller.Open("DEMO", 1U));

  const Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  CHECK(std::strcmp(snapshot.title.data(), "SAMPLES") == 0);
  CHECK(std::strcmp(snapshot.meta.data(), "01") == 0);
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
  REQUIRE(controller.OpenLibrary("DEMO", 2U));

  CHECK(controller.Mode() == Ui2SampleBrowserMode::Library);
  const Ui2BrowserSnapshot snapshot = controller.Snapshot(60);
  CHECK(std::strcmp(snapshot.title.data(), "IMPORT") == 0);
  CHECK(std::strcmp(snapshot.meta.data(), "02") == 0);
  REQUIRE(snapshot.visibleItemCount == 2U);
  CHECK(std::strcmp(snapshot.items[0].data(), "~KICK.WAV") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "/DRUMS") == 0);
  REQUIRE(snapshot.actionCount == 3U);
  CHECK(std::strcmp(snapshot.actions[0].data(), "IMPORT") == 0);
  CHECK(std::strcmp(snapshot.actions[2].data(), "BACK") == 0);
}

TEST_CASE("UI2 Sample Browser keeps project pool flat and root-addressed") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO", 1U));

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
  REQUIRE(controller.Open("DEMO", 1U));

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

TEST_CASE("UI2 Sample Browser parent chord cannot escape the library root") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.OpenLibrary("DEMO", 1U));

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
  REQUIRE(controller.Open("DEMO", 3U));

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
  REQUIRE(controller.Open("DEMO", 3U));

  controller.Handle(TrackerAction::Right, true);
  controller.Handle(TrackerAction::Right, true);
  controller.Handle(TrackerAction::Right, false);

  const Ui2BrowserSnapshot snapshot = controller.Snapshot(40);
  REQUIRE(snapshot.actionCount == 3U);
  CHECK(snapshot.activeAction == 2U);
  CHECK(std::strcmp(snapshot.actions[2].data(), "DELETE") == 0);
}

TEST_CASE("UI2 Sample Browser exposes actions for empty states") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem(true);
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO", 1U));

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

TEST_CASE("UI2 Sample Browser delete confirmation defaults to NO") {
  using namespace ui2;
  SampleBrowserFileSystem fileSystem;
  Ui2SampleBrowserController controller;
  REQUIRE(controller.Open("DEMO", 0U));
  Tap(controller, TrackerAction::Left); // DELETE wraps from EDIT
  const Ui2SampleBrowserCommand request = Tap(controller, TrackerAction::Edit);
  REQUIRE(request.type == Ui2SampleBrowserCommandType::RequestDelete);
  controller.RequestDeleteConfirmation(request.filename.data());
  REQUIRE(controller.DialogActive());
  CHECK(controller.DialogSnapshot().selectedAction == 1U);
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
