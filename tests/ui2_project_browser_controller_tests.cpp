#include "doctest/doctest.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

// TinyXML's embedded adapter remaps stdio names, so host standard-library
// headers must precede production persistence headers.
#include "Application/UI2/Controllers/Ui2ProjectBrowserController.h"

namespace {

class ProjectBrowserFileSystem final : public FileSystem {
public:
  explicit ProjectBrowserFileSystem(int projectCount = 0)
      : projectCount_(projectCount) {
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(this);
  }
  ~ProjectBrowserFileSystem() override { FileSystem::Install(previous_); }

  FileHandle Open(const char *, const char *) override { return {}; }
  bool chdir(const char *path) override {
    (void)path;
    ++chdirCalls_;
    return false;
  }
  void list(etl::ivector<int> *indices, const char *, bool,
            bool includeHidden = false) override {
    legacyIncludedHidden_ = includeHidden;
    indices->clear();
    legacyListingOverwritten_ = true;
    indices->push_back(0);
  }
  bool listPathChecked(const char *path,
                       FileSystemDirectorySnapshot &snapshot, const char *,
                       bool, bool includeHidden) override {
    if (path == nullptr)
      return false;
    lastPath_ = path;
    includedHidden_ = includeHidden;
    snapshot.Reset();
    const char *const projects[] = {
        "OLD",
        "ACTIVE",
        UNNAMED_PROJECT_NAME,
        STAGING_BACKUP_PROJECT_NAME,
        ".picotracker-saveas-stage.OLD",
        ".picotracker-saveas-backup.OLD",
        // Valid historical user name: old firmware could create this, so the
        // new longer journal namespace must neither hide nor recover/delete it.
        ".saveas-stage.X",
        // Leading-dot user projects other than the exact persistence-owned
        // names remain browser-visible.
        ".FOO",
    };
    if (std::strcmp(path, PROJECTS_DIR) == 0) {
      if (projectCount_ > 0) {
        for (int index = 0; index < projectCount_; ++index) {
          char name[MAX_PROJECT_NAME_LENGTH + 1U]{};
          std::snprintf(name, sizeof(name), "PROJECT%03d", index);
          if (!snapshot.Add(name, PFT_DIR, 0U))
            break;
        }
        return true;
      }
      for (const char *name : projects) {
        if (!snapshot.Add(name, PFT_DIR, 0U))
          break;
      }
      return true;
    }
    if (std::strcmp(path, "/") == 0)
      (void)snapshot.Add("projects", PFT_DIR, 0U);
    if (std::strcmp(path, "/") == 0)
      return true;
    return false;
  }
  void getFileName(int index, char *name, int length) override {
    if (name == nullptr || length <= 0)
      return;
    const char *value = legacyListingOverwritten_ && index == 0 ? "STALE" : "";
    std::snprintf(name, static_cast<std::size_t>(length), "%s", value);
  }
  PicoFileType getFileType(int index) override {
    return index == 0 ? PFT_DIR : PFT_UNKNOWN;
  }
  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return true; }
  bool DeleteFile(const char *) override { return false; }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *) override { return false; }
  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int) override { return 0U; }
  bool CopyFile(const char *, const char *) override { return false; }
  bool MoveFile(const char *, const char *) override { return false; }
  bool isExFat() override { return false; }
  [[nodiscard]] bool IncludedHidden() const { return includedHidden_; }
  [[nodiscard]] int ChdirCalls() const { return chdirCalls_; }
  [[nodiscard]] const std::string &LastPath() const { return lastPath_; }
  [[nodiscard]] bool LegacyIncludedHidden() const {
    return legacyIncludedHidden_;
  }

private:
  FileSystem *previous_ = nullptr;
  std::string lastPath_{};
  int chdirCalls_ = 0;
  bool includedHidden_ = false;
  bool legacyIncludedHidden_ = false;
  bool legacyListingOverwritten_ = false;
  int projectCount_ = 0;
};

template <typename Controller>
auto Tap(Controller &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

} // namespace

TEST_CASE("UI2 Project Browser reserves Option for M8-style chords") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));
  CHECK(fileSystem.IncludedHidden());
  const Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(snapshot.totalItemCount == 5U);
  CHECK(std::strcmp(snapshot.items[0].data(), "..") == 0);
  CHECK(std::strcmp(snapshot.items[4].data(), ".FOO") == 0);

  CHECK(controller.Handle(TrackerAction::Option, true).type ==
        Ui2ProjectBrowserCommandType::None);
  CHECK(controller.Snapshot().selectedRow == 1U);

  const Ui2ProjectBrowserCommand remove = Tap(controller, TrackerAction::Edit);
  CHECK(remove.type == Ui2ProjectBrowserCommandType::Delete);
  CHECK(std::strcmp(remove.project.data(), "OLD") == 0);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Project Browser owns names without changing filesystem cwd") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;

  REQUIRE(controller.Refresh("ACTIVE"));
  CHECK(fileSystem.ChdirCalls() == 0);
  CHECK(fileSystem.LastPath() == PROJECTS_DIR);

  etl::vector<int, 1> legacy;
  fileSystem.list(&legacy, "", true);
  REQUIRE(legacy.size() == 1U);
  CHECK_FALSE(fileSystem.LegacyIncludedHidden());

  // A different legacy caller may replace its adapter's cached index table.
  // Project Browser must continue to render and emit its owned path snapshot.
  const Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(std::strcmp(snapshot.items[0].data(), "..") == 0);
  CHECK(std::strcmp(snapshot.actions[0].data(), "LOAD") == 0);
  const Ui2ProjectBrowserCommand command =
      Tap(controller, TrackerAction::Edit);
  CHECK(command.type == Ui2ProjectBrowserCommandType::Load);
  CHECK(std::strcmp(command.project.data(), "OLD") == 0);
}

TEST_CASE("UI2 Project Browser accepts held-direction repeat pulses") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));

  controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, true);
  controller.Handle(TrackerAction::Down, false);

  const Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(snapshot.selectedRow == 3U);
  CHECK(std::strcmp(snapshot.items[3].data(), ".saveas-stage.X") == 0);
}

TEST_CASE("UI2 Project Browser option directions jump eight entries") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));

  controller.Handle(TrackerAction::Option, true);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.Snapshot().selectedRow == 4U);
  Tap(controller, TrackerAction::Up);
  CHECK(controller.Snapshot().selectedRow == 0U);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Project Browser dot-dot navigates to the SD-card root") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));

  Tap(controller, TrackerAction::Up);
  CHECK(std::strcmp(controller.Snapshot().actions[0].data(), "UP") == 0);
  const Ui2ProjectBrowserCommand command =
      Tap(controller, TrackerAction::Edit);
  CHECK(fileSystem.ChdirCalls() == 0);
  CHECK(command.type == Ui2ProjectBrowserCommandType::None);
  CHECK(fileSystem.LastPath() == "/");
  Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(std::strcmp(snapshot.meta.data(), "/") == 0);
  CHECK(snapshot.totalItemCount == 1U);
  CHECK(std::strcmp(snapshot.items[0].data(), "projects") == 0);
  CHECK(std::strcmp(snapshot.actions[0].data(), "OPEN") == 0);

  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2ProjectBrowserCommandType::None);
  CHECK(fileSystem.LastPath() == PROJECTS_DIR);
  snapshot = controller.Snapshot();
  CHECK(std::strcmp(snapshot.items[0].data(), "..") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "OLD") == 0);
}

TEST_CASE("UI2 Project Browser keeps Load explicit and protects active Delete") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));

  const Ui2ProjectBrowserCommand load = Tap(controller, TrackerAction::Edit);
  CHECK(load.type == Ui2ProjectBrowserCommandType::Load);
  CHECK(std::strcmp(load.project.data(), "OLD") == 0);

  Tap(controller, TrackerAction::Down); // ACTIVE
  controller.Handle(TrackerAction::Option, true);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2ProjectBrowserCommandType::None);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Project Browser restores a failed load selection after refresh") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;

  REQUIRE(controller.RefreshAndSelect("ACTIVE", "OLD"));
  const Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(snapshot.selectedRow == 1U);
  CHECK(std::strcmp(snapshot.items[0].data(), "..") == 0);
  CHECK(std::strcmp(snapshot.items[1].data(), "OLD") == 0);
  CHECK(std::strcmp(snapshot.items[2].data(), "*ACTIVE") == 0);
}

TEST_CASE("UI2 Project Browser rejects a potentially truncated project scan") {
  using namespace ui2;
  SUBCASE("exact capacity remains a complete scan") {
    ProjectBrowserFileSystem fileSystem(MAX_FILE_INDEX_SIZE);
    Ui2ProjectBrowserController controller;

    CHECK(controller.Refresh("ACTIVE"));
    CHECK(controller.Snapshot().totalItemCount == MAX_FILE_INDEX_SIZE);
  }

  SUBCASE("one project beyond capacity fails closed") {
    ProjectBrowserFileSystem fileSystem(MAX_FILE_INDEX_SIZE + 1);
    Ui2ProjectBrowserController controller;

    CHECK_FALSE(controller.Refresh("ACTIVE"));
    CHECK(controller.Snapshot().totalItemCount == 0U);
  }
}

TEST_CASE("UI2 Project Browser owner releases clear a modal-opening chord") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));

  controller.Handle(TrackerAction::Option, true);
  const Ui2ProjectBrowserCommand remove =
      controller.Handle(TrackerAction::Edit, true);
  REQUIRE(remove.type == Ui2ProjectBrowserCommandType::Delete);

  // The confirmation consumes these releases too. Ui2TrackerApplication must
  // additionally return each key-up to the page that owned its key-down.
  controller.Handle(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Edit, false);
  const Ui2ProjectBrowserCommand load = Tap(controller, TrackerAction::Edit);
  CHECK(load.type == Ui2ProjectBrowserCommandType::Load);
  CHECK(std::strcmp(load.project.data(), "OLD") == 0);
}

static_assert(ui2::Ui2ProjectBrowserProjectAction(false, 0U) ==
              ui2::Ui2ProjectBrowserCommandType::Load);
static_assert(ui2::Ui2ProjectBrowserProjectAction(true, 0U) ==
              ui2::Ui2ProjectBrowserCommandType::Delete);
static_assert(ui2::Ui2ProjectBrowserProjectAction(false, 1U) ==
              ui2::Ui2ProjectBrowserCommandType::Delete);
