#include "doctest/doctest.h"

#include <array>
#include <cstdio>
#include <cstring>

// TinyXML's embedded adapter remaps stdio names, so host standard-library
// headers must precede production persistence headers.
#include "Application/UI2/Controllers/Ui2ProjectBrowserController.h"

namespace {

class ProjectBrowserFileSystem final : public FileSystem {
public:
  ProjectBrowserFileSystem() {
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(this);
  }
  ~ProjectBrowserFileSystem() override { FileSystem::Install(previous_); }

  FileHandle Open(const char *, const char *) override { return {}; }
  bool chdir(const char *path) override {
    if (std::strcmp(path, PROJECTS_DIR) == 0) {
      inProjects_ = true;
      return true;
    }
    if (std::strcmp(path, "..") == 0) {
      inProjects_ = false;
      return true;
    }
    return false;
  }
  void list(etl::ivector<int> *indices, const char *, bool,
            bool includeHidden = false) override {
    includedHidden_ = includeHidden;
    indices->clear();
    if (inProjects_) {
      indices->push_back(0);
      indices->push_back(1);
      indices->push_back(2);
      indices->push_back(3);
      indices->push_back(4);
      indices->push_back(5);
      indices->push_back(6);
      indices->push_back(7);
    }
  }
  void getFileName(int index, char *name, int length) override {
    if (name == nullptr || length <= 0)
      return;
    const char *value = index == 0   ? "OLD"
                        : index == 1 ? "ACTIVE"
                        : index == 2 ? UNNAMED_PROJECT_NAME
                        : index == 3 ? STAGING_BACKUP_PROJECT_NAME
                        : index == 4 ? ".picotracker-saveas-stage.OLD"
                        : index == 5 ? ".picotracker-saveas-backup.OLD"
                        // Valid historical user name: old firmware could
                        // create this, so the new longer journal namespace
                        // must neither hide nor recover/delete it.
                        : index == 6 ? ".saveas-stage.X"
                        // Leading-dot user projects other than the exact
                        // persistence-owned names remain browser-visible.
                        : index == 7 ? ".FOO"
                                     : "";
    std::snprintf(name, static_cast<std::size_t>(length), "%s", value);
  }
  PicoFileType getFileType(int index) override {
    return index >= 0 && index <= 7 ? PFT_DIR : PFT_UNKNOWN;
  }
  bool isParentRoot() override { return inProjects_; }
  bool isCurrentRoot() override { return !inProjects_; }
  bool DeleteFile(const char *) override { return false; }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *) override { return false; }
  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int) override { return 0U; }
  bool CopyFile(const char *, const char *) override { return false; }
  bool MoveFile(const char *, const char *) override { return false; }
  bool isExFat() override { return false; }
  [[nodiscard]] bool IncludedHidden() const { return includedHidden_; }

private:
  FileSystem *previous_ = nullptr;
  bool inProjects_ = false;
  bool includedHidden_ = false;
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
  CHECK(snapshot.totalItemCount == 5U); // .. + four user projects
  CHECK(std::strcmp(snapshot.items[4].data(), ".FOO") == 0);

  CHECK(controller.Handle(TrackerAction::Option, true).type ==
        Ui2ProjectBrowserCommandType::None);
  CHECK(controller.Snapshot().selectedRow == 0U); // parent row

  // Option+Down follows legacy warp semantics: exactly one ordinary item.
  CHECK(Tap(controller, TrackerAction::Down).type ==
        Ui2ProjectBrowserCommandType::None);
  CHECK(controller.Snapshot().selectedRow == 1U);

  const Ui2ProjectBrowserCommand remove = Tap(controller, TrackerAction::Edit);
  CHECK(remove.type == Ui2ProjectBrowserCommandType::Delete);
  CHECK(std::strcmp(remove.project.data(), "OLD") == 0);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Project Browser keeps Load explicit and protects active Delete") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));

  Tap(controller, TrackerAction::Down); // OLD
  const Ui2ProjectBrowserCommand load = Tap(controller, TrackerAction::Edit);
  CHECK(load.type == Ui2ProjectBrowserCommandType::Load);
  CHECK(std::strcmp(load.project.data(), "OLD") == 0);

  controller.Handle(TrackerAction::Option, true);
  Tap(controller, TrackerAction::Down); // ACTIVE
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2ProjectBrowserCommandType::None);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Project Browser owner releases clear a modal-opening chord") {
  using namespace ui2;
  ProjectBrowserFileSystem fileSystem;
  Ui2ProjectBrowserController controller;
  REQUIRE(controller.Refresh("ACTIVE"));
  Tap(controller, TrackerAction::Down); // OLD

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
