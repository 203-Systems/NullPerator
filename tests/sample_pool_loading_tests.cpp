#include "Application/Instruments/SampleBindingState.h"
#include "Application/Instruments/SamplePoolLoading.h"

#include "doctest/doctest.h"

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
  void getFileName(int, char *, int) override {}
  PicoFileType getFileType(int) override { return PFT_FILE; }
  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return false; }
  bool DeleteFile(const char *) override { return false; }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *) override { return false; }
  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int) override { return 0; }
  bool CopyFile(const char *, const char *) override { return false; }
  bool MoveFile(const char *, const char *) override { return false; }
  bool isExFat() override { return false; }

  int failChdirCall = 0;
  int chdirCalls = 0;
  int listCalls = 0;
  int listedEntries = 0;
  bool listSucceeds = true;
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
