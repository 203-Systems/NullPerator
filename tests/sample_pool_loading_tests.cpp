#include "Application/Instruments/SampleBindingState.h"
#include "Application/Instruments/SamplePoolLoading.h"

#include "doctest/doctest.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {
class SampleListingFileSystem final : public FileSystem {
public:
  FileHandle Open(const char *, const char *) override { return {}; }
  bool chdir(const char *) override {
    ++chdirCalls;
    return failChdirCall == 0 || chdirCalls != failChdirCall;
  }
  void list(etl::ivector<int> *indexes, const char *, bool,
            bool = false) override {
    (void)listChecked(indexes, nullptr, false);
  }
  bool listChecked(etl::ivector<int> *indexes, const char *, bool,
                   bool = false) override {
    ++listCalls;
    indexes->clear();
    if (!listSucceeds)
      return false;
    for (int index = 0; index < listedEntries && !indexes->full(); ++index)
      indexes->push_back(index);
    return true;
  }
  void getFileName(int index, char *name, int length) override {
    if (name == nullptr || length <= 0 || index < 0 ||
        static_cast<std::size_t>(index) >= entryNames.size()) {
      return;
    }
    std::strncpy(name, entryNames[static_cast<std::size_t>(index)].data(),
                 static_cast<std::size_t>(length - 1));
    name[length - 1] = '\0';
  }
  PicoFileType getFileType(int index) override {
    if (index < 0 || static_cast<std::size_t>(index) >= entryTypes.size())
      return PFT_UNKNOWN;
    return entryTypes[static_cast<std::size_t>(index)];
  }
  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return false; }
  bool DeleteFile(const char *) override { return false; }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *) override { return pathExists; }
  bool makeDir(const char *, bool = false) override {
    ++makeDirCalls;
    return makeDirSucceeds;
  }
  std::uint64_t getFileSize(int) override { return 0; }
  bool CopyFile(const char *, const char *) override { return false; }
  bool MoveFile(const char *, const char *) override { return false; }
  bool isExFat() override { return false; }

  void SetEntry(int index, const char *name, PicoFileType type) {
    REQUIRE(index >= 0);
    REQUIRE(static_cast<std::size_t>(index) < entryNames.size());
    std::strncpy(entryNames[static_cast<std::size_t>(index)].data(), name,
                 PFILENAME_SIZE - 1U);
    entryNames[static_cast<std::size_t>(index)][PFILENAME_SIZE - 1U] = '\0';
    entryTypes[static_cast<std::size_t>(index)] = type;
  }

  int failChdirCall = 0;
  int chdirCalls = 0;
  int listCalls = 0;
  int listedEntries = 0;
  bool listSucceeds = true;
  bool pathExists = false;
  bool makeDirSucceeds = false;
  int makeDirCalls = 0;
  std::array<std::array<char, PFILENAME_SIZE>, 8> entryNames{};
  std::array<PicoFileType, 8> entryTypes{};
};
} // namespace

TEST_CASE("sample pool listing distinguishes navigation failure from empty") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 4> indexes;
  filesystem.failChdirCall = 2;

  CHECK_FALSE(
      SamplePoolLoading::EnterAndList(filesystem, "SONG", indexes));
  CHECK(indexes.empty());
  CHECK(filesystem.listCalls == 0);
}

TEST_CASE("sample pool listing propagates enumeration failure") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 4> indexes;
  filesystem.listSucceeds = false;

  CHECK_FALSE(
      SamplePoolLoading::EnterAndList(filesystem, "SONG", indexes));
  CHECK(indexes.empty());
  CHECK(filesystem.listCalls == 1);
}

TEST_CASE("sample pool listing rejects a capacity-ambiguous result") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 2> indexes;
  filesystem.listedEntries = 2;

  CHECK_FALSE(
      SamplePoolLoading::EnterAndList(filesystem, "SONG", indexes));
  CHECK(indexes.empty());
}

TEST_CASE("sample pool listing accepts a checked empty directory") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 4> indexes;

  CHECK(SamplePoolLoading::EnterAndList(filesystem, "SONG", indexes));
  CHECK(indexes.empty());
  CHECK(filesystem.chdirCalls == 3);
}

TEST_CASE("sample pool recreates a missing legacy samples directory") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 4> indexes;
  filesystem.failChdirCall = 3;
  filesystem.makeDirSucceeds = true;

  CHECK(SamplePoolLoading::EnterAndList(filesystem, "LEGACY", indexes));
  CHECK(indexes.empty());
  CHECK(filesystem.makeDirCalls == 1);
  CHECK(filesystem.chdirCalls == 4);
  // Backup and orphan-working recovery scans, then the visible WAV listing.
  CHECK(filesystem.listCalls == 3);
}

TEST_CASE("sample pool accepts a missing legacy samples directory read-only") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 4> indexes;
  filesystem.failChdirCall = 3;

  CHECK(SamplePoolLoading::EnterAndList(filesystem, "LEGACY", indexes));
  CHECK(indexes.empty());
  CHECK(filesystem.makeDirCalls == 1);
  CHECK(filesystem.listCalls == 0);
}

TEST_CASE("sample pool rejects an inaccessible existing samples directory") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 4> indexes;
  filesystem.failChdirCall = 3;
  filesystem.pathExists = true;

  CHECK_FALSE(SamplePoolLoading::EnterAndList(filesystem, "BROKEN", indexes));
  CHECK(indexes.empty());
  CHECK(filesystem.makeDirCalls == 0);
  CHECK(filesystem.listCalls == 0);
}

TEST_CASE("sample capacity ignores directory navigation rows") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 8> indexes;
  filesystem.SetEntry(0, "..", PFT_DIR);
  indexes.push_back(0);
  for (int index = 1; index <= 4; ++index) {
    const char *name = index == 1   ? "A.wav"
                       : index == 2 ? "B.wav"
                       : index == 3 ? "C.wav"
                                    : "D.wav";
    filesystem.SetEntry(index, name, PFT_FILE);
    indexes.push_back(index);
  }

  CHECK(SamplePoolLoading::FitsLoadableSampleCapacity(filesystem, indexes, 0,
                                                       4));
}

TEST_CASE("sample capacity rejects only excess loadable files") {
  SampleListingFileSystem filesystem;
  etl::vector<int, 8> indexes;
  filesystem.SetEntry(0, "..", PFT_DIR);
  filesystem.SetEntry(1, "filename-that-is-too-long.wav", PFT_FILE);
  indexes.push_back(0);
  indexes.push_back(1);
  for (int index = 2; index <= 5; ++index) {
    const char *name = index == 2   ? "A.wav"
                       : index == 3 ? "B.wav"
                       : index == 4 ? "C.wav"
                                    : "D.wav";
    filesystem.SetEntry(index, name, PFT_FILE);
    indexes.push_back(index);
  }

  CHECK(SamplePoolLoading::FitsLoadableSampleCapacity(filesystem, indexes, 0,
                                                       4));
  filesystem.SetEntry(6, "E.wav", PFT_FILE);
  indexes.push_back(6);
  CHECK_FALSE(SamplePoolLoading::FitsLoadableSampleCapacity(
      filesystem, indexes, 0, 4));
}

TEST_CASE("missing sample binding survives a save and reload cycle") {
  SampleBindingState firstLoad;
  firstLoad.Capture("missing-kick.wav", -1);
  REQUIRE(firstLoad.HasUnresolvedName());
  const etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> persisted(
      firstLoad.UnresolvedName());

  SampleBindingState reloaded;
  reloaded.Capture(persisted.c_str(), -1);
  CHECK(reloaded.HasUnresolvedName());
  CHECK(std::strcmp(reloaded.UnresolvedName(), persisted.c_str()) == 0);

  reloaded.Capture("missing-kick.wav", 3);
  CHECK_FALSE(reloaded.HasUnresolvedName());
}
