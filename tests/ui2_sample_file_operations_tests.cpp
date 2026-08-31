#include "doctest/doctest.h"

#include "Application/UI2/Ui2SampleFileOperations.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr const char *kSource = "/projects/DEMO/samples/AKWF.WAV";
constexpr const char *kStage =
    "/projects/DEMO/samples/.ui2-sample-delete.AKWF.WAV.tmp";

class FailureFileSystem final : public FileSystem {
public:
  FileHandle Open(const char *, const char *) override { return {}; }

  bool chdir(const char *path) override {
    if (failNavigation_)
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
      if (sampleDirectoryMissing_)
        return false;
      directory_ = Directory::Samples;
      return true;
    }
    return false;
  }

  void list(etl::ivector<int> *indices, const char *filter, bool subDirOnly,
            bool includeHidden = false) override {
    (void)listChecked(indices, filter, subDirOnly, includeHidden);
  }

  bool listChecked(etl::ivector<int> *indices, const char *filter, bool,
                   bool includeHidden = false) override {
    indices->clear();
    listed_.clear();
    if (failListing_ || directory_ != Directory::Samples)
      return false;
    const std::string needle = filter == nullptr ? "" : filter;
    constexpr const char *prefix = "/projects/DEMO/samples/";
    for (const std::string &path : files_) {
      if (!path.starts_with(prefix))
        continue;
      const std::string leaf = path.substr(std::strlen(prefix));
      if ((!includeHidden && leaf.starts_with('.')) ||
          (!needle.empty() && leaf.find(needle) == std::string::npos)) {
        continue;
      }
      listed_.push_back(leaf);
      indices->push_back(static_cast<int>(listed_.size() - 1U));
    }
    return true;
  }

  void getFileName(int index, char *name, int length) override {
    if (name == nullptr || length <= 0)
      return;
    const char *value =
        index >= 0 && static_cast<std::size_t>(index) < listed_.size()
            ? listed_[static_cast<std::size_t>(index)].c_str()
            : "";
    std::snprintf(name, static_cast<std::size_t>(length), "%s", value);
  }

  PicoFileType getFileType(int index) override {
    return index >= 0 && static_cast<std::size_t>(index) < listed_.size()
               ? PFT_FILE
               : PFT_UNKNOWN;
  }

  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return false; }

  bool DeleteFile(const char *path) override {
    if (path == nullptr || failedDeletes_.contains(path))
      return false;
    return files_.erase(path) == 1U;
  }

  bool DeleteDir(const char *) override { return false; }

  bool exists(const char *path) override {
    if (path != nullptr && std::strcmp(path, PROJECT_SAMPLES_DIR) == 0)
      return !sampleDirectoryMissing_;
    return path != nullptr && files_.contains(path);
  }

  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int) override { return 0U; }
  bool CopyFile(const char *, const char *) override { return false; }

  bool MoveFile(const char *source, const char *destination) override {
    if (source == nullptr || destination == nullptr ||
        failedMoves_.contains({source, destination}) ||
        !files_.contains(source) || files_.contains(destination)) {
      return false;
    }
    files_.erase(source);
    files_.insert(destination);
    return true;
  }

  bool isExFat() override { return false; }

  void Add(const char *path) { files_.insert(path); }
  void FailDelete(const char *path) { failedDeletes_.insert(path); }
  void FailMove(const char *source, const char *destination) {
    failedMoves_.insert({source, destination});
  }
  void ClearFailures() {
    failedDeletes_.clear();
    failedMoves_.clear();
    failListing_ = false;
    failNavigation_ = false;
  }
  void SetListingFails(bool fails) { failListing_ = fails; }
  void SetNavigationFails(bool fails) { failNavigation_ = fails; }
  void SetSampleDirectoryMissing(bool missing) {
    sampleDirectoryMissing_ = missing;
  }

private:
  enum class Directory { Root, Projects, Project, Samples };
  std::set<std::string> files_{};
  std::set<std::string> failedDeletes_{};
  std::set<std::pair<std::string, std::string>> failedMoves_{};
  std::vector<std::string> listed_{};
  Directory directory_ = Directory::Root;
  bool failListing_ = false;
  bool failNavigation_ = false;
  bool sampleDirectoryMissing_ = false;
};

} // namespace

TEST_CASE("UI2 sample delete rolls back an unload failure") {
  FailureFileSystem fileSystem;
  fileSystem.Add(kSource);
  SamplePool pool;
  pool.SetSample("AKWF.WAV");
  pool.SetUnloadFails(true);

  CHECK(
      ui2::Ui2DeleteProjectSampleSafely(fileSystem, pool, "DEMO", "AKWF.WAV") ==
      ui2::Ui2DeleteProjectSampleResult::UnloadFailed);
  CHECK(fileSystem.exists(kSource));
  CHECK_FALSE(fileSystem.exists(kStage));
  CHECK(pool.GetNameListSize() == 1);
}

TEST_CASE("UI2 sample delete reports and recovers a failed rollback") {
  FailureFileSystem fileSystem;
  fileSystem.Add(kSource);
  fileSystem.FailMove(kStage, kSource);
  SamplePool pool;
  pool.SetSample("AKWF.WAV");
  pool.SetUnloadFails(true);

  CHECK(
      ui2::Ui2DeleteProjectSampleSafely(fileSystem, pool, "DEMO", "AKWF.WAV") ==
      ui2::Ui2DeleteProjectSampleResult::RollbackFailed);
  CHECK_FALSE(fileSystem.exists(kSource));
  CHECK(fileSystem.exists(kStage));

  fileSystem.ClearFailures();
  CHECK(ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, "DEMO"));
  CHECK(fileSystem.exists(kSource));
  CHECK_FALSE(fileSystem.exists(kStage));
}

TEST_CASE("UI2 sample delete cleanup can be retried idempotently") {
  FailureFileSystem fileSystem;
  fileSystem.Add(kSource);
  fileSystem.FailDelete(kStage);
  SamplePool pool;
  pool.SetSample("AKWF.WAV");

  CHECK(
      ui2::Ui2DeleteProjectSampleSafely(fileSystem, pool, "DEMO", "AKWF.WAV") ==
      ui2::Ui2DeleteProjectSampleResult::CleanupFailed);
  CHECK_FALSE(fileSystem.exists(kSource));
  CHECK(fileSystem.exists(kStage));
  CHECK(pool.GetNameListSize() == 0);

  fileSystem.ClearFailures();
  CHECK(
      ui2::Ui2DeleteProjectSampleSafely(fileSystem, pool, "DEMO", "AKWF.WAV") ==
      ui2::Ui2DeleteProjectSampleResult::Deleted);
  CHECK_FALSE(fileSystem.exists(kStage));
}

TEST_CASE("UI2 sample delete stages do not block another sample") {
  FailureFileSystem fileSystem;
  fileSystem.Add(kStage);
  fileSystem.Add("/projects/DEMO/samples/.ui2-sample-delete.tmp");
  fileSystem.Add("/projects/DEMO/samples/KICK.WAV");
  SamplePool pool;
  pool.SetSample("KICK.WAV");

  CHECK(
      ui2::Ui2DeleteProjectSampleSafely(fileSystem, pool, "DEMO", "KICK.WAV") ==
      ui2::Ui2DeleteProjectSampleResult::Deleted);
  CHECK(fileSystem.exists(kStage));
  CHECK(fileSystem.exists("/projects/DEMO/samples/.ui2-sample-delete.tmp"));
  CHECK_FALSE(fileSystem.exists("/projects/DEMO/samples/KICK.WAV"));
}

TEST_CASE("UI2 sample delete restart recovery restores before pool load") {
  FailureFileSystem fileSystem;
  fileSystem.Add(kSource);
  fileSystem.FailDelete(kStage);
  SamplePool firstPool;
  firstPool.SetSample("AKWF.WAV");
  REQUIRE(ui2::Ui2DeleteProjectSampleSafely(fileSystem, firstPool, "DEMO",
                                            "AKWF.WAV") ==
          ui2::Ui2DeleteProjectSampleResult::CleanupFailed);

  fileSystem.ClearFailures();
  REQUIRE(ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, "DEMO"));
  CHECK(fileSystem.exists(kSource));
  CHECK_FALSE(fileSystem.exists(kStage));

  SamplePool restartedPool;
  restartedPool.SetSample("AKWF.WAV");
  CHECK(ui2::Ui2DeleteProjectSampleSafely(fileSystem, restartedPool, "DEMO",
                                          "AKWF.WAV") ==
        ui2::Ui2DeleteProjectSampleResult::Deleted);
}

TEST_CASE("UI2 sample delete recovery fails closed on conflicts and I/O") {
  SUBCASE("source and stage conflict") {
    FailureFileSystem fileSystem;
    fileSystem.Add(kSource);
    fileSystem.Add(kStage);
    CHECK_FALSE(ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, "DEMO"));
    CHECK(fileSystem.exists(kSource));
    CHECK(fileSystem.exists(kStage));
  }

  SUBCASE("listing fails") {
    FailureFileSystem fileSystem;
    fileSystem.SetListingFails(true);
    CHECK_FALSE(ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, "DEMO"));
  }

  SUBCASE("legacy empty sample directory remains valid") {
    FailureFileSystem fileSystem;
    fileSystem.SetSampleDirectoryMissing(true);
    CHECK(ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, "DEMO"));
  }

  SUBCASE("invalid project never navigates") {
    FailureFileSystem fileSystem;
    CHECK_FALSE(ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, ""));
    CHECK_FALSE(
        ui2::Ui2RecoverStagedProjectSampleDeletes(fileSystem, "../DEMO"));
  }
}
