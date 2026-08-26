#include "doctest/doctest.h"

#include "Application/Persistency/ProjectFileJournal.h"

#include <map>
#include <string>
#include <vector>

namespace {

constexpr project_file_journal::Paths kPaths{
    "/projects/demo/lgptsav.dat", "/projects/demo/lgptsav.tmp",
    "/projects/demo/lgptsav.bak"};

class JournalFileSystem {
public:
  bool exists(const char *path) const {
    return path != nullptr && files.contains(path);
  }

  bool DeleteFile(const char *path) {
    deleteAttempts.emplace_back(path == nullptr ? "" : path);
    if (path == nullptr || failDelete == path)
      return false;
    return files.erase(path) != 0U;
  }

  bool MoveFile(const char *source, const char *destination) {
    if (source == nullptr || destination == nullptr)
      return false;
    if (failMoveCount != 0U && failMoveDestination == destination) {
      --failMoveCount;
      return false;
    }
    const auto found = files.find(source);
    if (found == files.end() ||
        (refuseReplace && files.contains(destination))) {
      return false;
    }
    files[destination] = found->second;
    files.erase(found);
    return true;
  }

  std::map<std::string, std::string> files;
  std::vector<std::string> deleteAttempts;
  std::string failDelete;
  std::string failMoveDestination;
  unsigned failMoveCount = 0U;
  bool refuseReplace = false;
};

bool IsValid(const JournalFileSystem &fileSystem, const char *path) {
  const auto found = fileSystem.files.find(path);
  return found != fileSystem.files.end() && found->second != "invalid";
}

bool Save(JournalFileSystem &fileSystem) {
  return project_file_journal::SaveAtomically(
      fileSystem, kPaths,
      [&](const char *path) {
        fileSystem.files[path] = "new";
        return true;
      },
      [&](const char *path) { return IsValid(fileSystem, path); });
}

} // namespace

TEST_CASE("project journal recovery prefers backup over temporary") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "invalid";
  fileSystem.files[kPaths.temporary] = "temporary";
  fileSystem.files[kPaths.backup] = "backup";

  REQUIRE(project_file_journal::Recover(
      fileSystem, kPaths,
      [&](const char *path) { return IsValid(fileSystem, path); }));
  CHECK(fileSystem.files[kPaths.destination] == "backup");
  CHECK_FALSE(fileSystem.exists(kPaths.temporary));
  CHECK_FALSE(fileSystem.exists(kPaths.backup));
}

TEST_CASE("project journal preserves all bytes when no generation is valid") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "invalid";
  fileSystem.files[kPaths.temporary] = "invalid";
  fileSystem.files[kPaths.backup] = "invalid";

  CHECK(project_file_journal::Recover(
      fileSystem, kPaths,
      [&](const char *path) { return IsValid(fileSystem, path); }));
  CHECK(fileSystem.files.size() == 3U);
  CHECK(fileSystem.deleteAttempts.empty());
}

TEST_CASE("valid project destination retains backup until semantic finalize") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "current";
  fileSystem.files[kPaths.temporary] = "temporary";
  fileSystem.files[kPaths.backup] = "previous";
  fileSystem.failDelete = kPaths.temporary;

  CHECK(project_file_journal::Recover(
      fileSystem, kPaths,
      [&](const char *path) { return IsValid(fileSystem, path); }));
  CHECK(fileSystem.files[kPaths.destination] == "current");
  CHECK(fileSystem.files[kPaths.temporary] == "temporary");
  CHECK(fileSystem.files[kPaths.backup] == "previous");
}

TEST_CASE("project journal atomic save supports no-replace filesystems") {
  JournalFileSystem fileSystem;
  fileSystem.refuseReplace = true;
  fileSystem.files[kPaths.destination] = "old";

  REQUIRE(Save(fileSystem));
  CHECK(fileSystem.files[kPaths.destination] == "new");
  CHECK_FALSE(fileSystem.exists(kPaths.temporary));
  CHECK_FALSE(fileSystem.exists(kPaths.backup));
}

TEST_CASE("project journal retains backup if install and rollback both fail") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "old";
  fileSystem.failMoveDestination = kPaths.destination;
  fileSystem.failMoveCount = 3U;

  CHECK_FALSE(Save(fileSystem));
  CHECK_FALSE(fileSystem.exists(kPaths.destination));
  CHECK_FALSE(fileSystem.exists(kPaths.temporary));
  CHECK(fileSystem.files[kPaths.backup] == "old");
}

TEST_CASE("project journal cleanup failure does not fail committed save") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "old";
  fileSystem.files[kPaths.backup] = "previous";
  fileSystem.failDelete = kPaths.backup;

  CHECK(Save(fileSystem));
  CHECK(fileSystem.files[kPaths.destination] == "new");
  CHECK(fileSystem.files[kPaths.backup] == "previous");
}

TEST_CASE("project journal promotes backup then finalizes recovery siblings") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "rejected";
  fileSystem.files[kPaths.temporary] = "uncommitted";
  fileSystem.files[kPaths.backup] = "previous";

  REQUIRE(project_file_journal::PromoteBackup(
      fileSystem, kPaths,
      [&](const char *path) { return IsValid(fileSystem, path); }));
  CHECK(fileSystem.files[kPaths.destination] == "previous");
  CHECK_FALSE(fileSystem.exists(kPaths.temporary));
  CHECK_FALSE(fileSystem.exists(kPaths.backup));

  fileSystem.files[kPaths.temporary] = "stale";
  fileSystem.files[kPaths.backup] = "stale";
  CHECK(project_file_journal::Finalize(fileSystem, kPaths));
  CHECK_FALSE(fileSystem.exists(kPaths.temporary));
  CHECK_FALSE(fileSystem.exists(kPaths.backup));
}

TEST_CASE("project journal discard is ordered and fail closed") {
  JournalFileSystem fileSystem;
  fileSystem.files[kPaths.destination] = "authoritative";
  fileSystem.files[kPaths.temporary] = "temporary";
  fileSystem.files[kPaths.backup] = "backup";
  fileSystem.failDelete = kPaths.backup;

  CHECK_FALSE(project_file_journal::Discard(fileSystem, kPaths));
  REQUIRE(fileSystem.deleteAttempts.size() == 2U);
  CHECK(fileSystem.deleteAttempts[0] == kPaths.temporary);
  CHECK(fileSystem.deleteAttempts[1] == kPaths.backup);
  CHECK_FALSE(fileSystem.exists(kPaths.temporary));
  CHECK(fileSystem.exists(kPaths.backup));
  CHECK(fileSystem.files[kPaths.destination] == "authoritative");
}
