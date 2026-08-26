#include "doctest/doctest.h"

#include "Application/Persistency/InstrumentExportTransaction.h"

#include <map>
#include <string>

namespace {

class JournalFileSystem {
public:
  bool exists(const char *path) const {
    return path != nullptr && files.contains(path);
  }

  bool DeleteFile(const char *path) {
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
    if (found == files.end() || (refuseReplace && files.contains(destination)))
      return false;
    files[destination] = found->second;
    files.erase(found);
    return true;
  }

  std::map<std::string, bool> files;
  std::string failDelete;
  std::string failMoveDestination;
  unsigned failMoveCount = 0U;
  bool refuseReplace = false;
};

InstrumentExportTransactionResult Export(JournalFileSystem &fileSystem,
                                         bool overwrite,
                                         bool writeSucceeds = true,
                                         bool generatedValid = true) {
  return ExportInstrumentFileAtomically(
      fileSystem, "/instruments/lead.pti", "/instruments/lead.tmp",
      "/instruments/lead.bak", overwrite,
      [&](const char *path) {
        fileSystem.files[path] = generatedValid;
        return writeSucceeds;
      },
      [&](const char *path) {
        const auto found = fileSystem.files.find(path);
        return found != fileSystem.files.end() && found->second;
      });
}

} // namespace

TEST_CASE("instrument export sibling paths reject hidden and traversing names") {
  char destination[64]{};
  char temporary[64]{};
  char backup[64]{};
  CHECK(BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                          "/instruments", "lead"));
  CHECK(std::string(destination) == "/instruments/lead.pti");
  CHECK(std::string(temporary) == "/instruments/lead.tmp");
  CHECK(std::string(backup) == "/instruments/lead.bak");
  CHECK_FALSE(BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                                "/instruments", ".hidden"));
  CHECK_FALSE(BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                                "/instruments", "../lead"));
  CHECK_FALSE(BuildInstrumentExportSiblingPaths(destination, temporary, backup,
                                                "/instruments", "a\\b"));
}

TEST_CASE("instrument export does not mutate an existing file without overwrite") {
  JournalFileSystem fileSystem;
  fileSystem.files["/instruments/lead.pti"] = true;
  bool wrote = false;
  const auto result = ExportInstrumentFileAtomically(
      fileSystem, "/instruments/lead.pti", "/instruments/lead.tmp",
      "/instruments/lead.bak", false,
      [&](const char *) {
        wrote = true;
        return true;
      },
      [&](const char *path) { return fileSystem.files[path]; });
  CHECK(result == InstrumentExportTransactionResult::Exists);
  CHECK_FALSE(wrote);
  CHECK(fileSystem.files["/instruments/lead.pti"]);
}

TEST_CASE("instrument export removes failed or invalid temporary output") {
  JournalFileSystem fileSystem;
  fileSystem.files["/instruments/lead.pti"] = true;

  CHECK(Export(fileSystem, true, false, true) ==
        InstrumentExportTransactionResult::Error);
  CHECK(fileSystem.files["/instruments/lead.pti"]);
  CHECK_FALSE(fileSystem.exists("/instruments/lead.tmp"));

  CHECK(Export(fileSystem, true, true, false) ==
        InstrumentExportTransactionResult::Error);
  CHECK(fileSystem.files["/instruments/lead.pti"]);
  CHECK_FALSE(fileSystem.exists("/instruments/lead.tmp"));
}

TEST_CASE("instrument export uses a recoverable backup on no-replace filesystems") {
  JournalFileSystem fileSystem;
  fileSystem.refuseReplace = true;
  fileSystem.files["/instruments/lead.pti"] = true;
  CHECK(Export(fileSystem, true) == InstrumentExportTransactionResult::Saved);
  CHECK(fileSystem.files["/instruments/lead.pti"]);
  CHECK_FALSE(fileSystem.exists("/instruments/lead.tmp"));
  CHECK_FALSE(fileSystem.exists("/instruments/lead.bak"));
}

TEST_CASE("instrument export restores the prior file when install fails") {
  JournalFileSystem fileSystem;
  fileSystem.refuseReplace = true;
  fileSystem.files["/instruments/lead.pti"] = true;
  fileSystem.failMoveDestination = "/instruments/lead.pti";
  fileSystem.failMoveCount = 2U;
  CHECK(Export(fileSystem, true) == InstrumentExportTransactionResult::Error);
  CHECK(fileSystem.files["/instruments/lead.pti"]);
  CHECK_FALSE(fileSystem.exists("/instruments/lead.bak"));
  CHECK_FALSE(fileSystem.exists("/instruments/lead.tmp"));
}

TEST_CASE("instrument export recovers a retained valid backup before writing") {
  JournalFileSystem fileSystem;
  fileSystem.files["/instruments/lead.bak"] = true;
  CHECK(Export(fileSystem, false) == InstrumentExportTransactionResult::Exists);
  CHECK(fileSystem.files["/instruments/lead.pti"]);
  CHECK_FALSE(fileSystem.exists("/instruments/lead.bak"));
}
