#include "doctest/doctest.h"

#include "Application/Instruments/SamplePoolLoading.h"
#include "Application/UI2/Ui2SampleEditorTransaction.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

class SampleEditMemoryFile final : public I_File {
public:
  SampleEditMemoryFile(std::vector<std::uint8_t> &bytes, bool writable,
                       const bool &failSync, std::uint32_t &syncCalls,
                       const std::uint32_t &failSyncAfter)
      : bytes_(bytes), failSync_(failSync), syncCalls_(syncCalls),
        failSyncAfter_(failSyncAfter), writable_(writable) {}

  int Read(void *destination, int size) override {
    if (destination == nullptr || size <= 0)
      return 0;
    const std::size_t count = std::min<std::size_t>(
        static_cast<std::size_t>(size), bytes_.size() - position_);
    if (count != 0U) {
      std::memcpy(destination, bytes_.data() + position_, count);
      position_ += count;
    }
    return static_cast<int>(count);
  }

  int GetC() override {
    return position_ < bytes_.size() ? bytes_[position_++] : -1;
  }

  int Write(const void *source, int size, int count) override {
    if (!writable_ || source == nullptr || size <= 0 || count <= 0)
      return 0;
    const std::size_t bytes =
        static_cast<std::size_t>(size) * static_cast<std::size_t>(count);
    if (position_ + bytes > bytes_.size())
      bytes_.resize(position_ + bytes);
    std::memcpy(bytes_.data() + position_, source, bytes);
    position_ += bytes;
    return static_cast<int>(bytes);
  }

  void Seek(long offset, int whence) override {
    std::int64_t base = 0;
    if (whence == SEEK_CUR)
      base = static_cast<std::int64_t>(position_);
    else if (whence == SEEK_END)
      base = static_cast<std::int64_t>(bytes_.size());
    const std::int64_t next = base + offset;
    if (next < 0) {
      error_ = true;
      position_ = 0U;
      return;
    }
    position_ = static_cast<std::size_t>(next);
    if (position_ > bytes_.size()) {
      if (writable_)
        bytes_.resize(position_);
      else {
        position_ = bytes_.size();
        error_ = true;
      }
    }
  }

  long Tell() override { return static_cast<long>(position_); }
  int Error() override { return error_ ? 1 : 0; }
  bool Sync() override {
    ++syncCalls_;
    return !failSync_ || syncCalls_ <= failSyncAfter_;
  }
  void Dispose() override { delete this; }

protected:
  bool Close() override { return true; }

private:
  std::vector<std::uint8_t> &bytes_;
  const bool &failSync_;
  std::uint32_t &syncCalls_;
  const std::uint32_t &failSyncAfter_;
  std::size_t position_ = 0U;
  bool writable_ = false;
  bool error_ = false;
};

class SampleEditMemoryFileSystem final : public FileSystem {
public:
  SampleEditMemoryFileSystem() {
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(this);
  }
  ~SampleEditMemoryFileSystem() override { FileSystem::Install(previous_); }

  FileHandle Open(const char *name, const char *mode) override {
    if (name == nullptr || mode == nullptr)
      return {};
    auto found = files_.find(name);
    const bool create = std::strchr(mode, 'w') != nullptr;
    if (found == files_.end() && create && !failCopy_)
      found = files_.emplace(name, std::vector<std::uint8_t>{}).first;
    if (found == files_.end())
      return {};
    const bool writable = std::strchr(mode, '+') != nullptr ||
                          std::strchr(mode, 'w') != nullptr;
    return MakeFileHandle(new SampleEditMemoryFile(
        found->second, writable, failSync_, syncCalls_, failSyncAfter_));
  }

  bool chdir(const char *) override { return true; }
  void list(etl::ivector<int> *entries, const char *, bool,
            bool = false) override {
    listed_.clear();
    entries->clear();
    for (const auto &[name, bytes] : files_) {
      (void)bytes;
      if (name.find('/') != std::string::npos)
        continue;
      listed_.push_back(name);
      entries->push_back(static_cast<int>(listed_.size() - 1U));
    }
  }
  void getFileName(int index, char *destination, int capacity) override {
    if (destination == nullptr || capacity <= 0 || index < 0 ||
        static_cast<std::size_t>(index) >= listed_.size())
      return;
    std::snprintf(destination, static_cast<std::size_t>(capacity), "%s",
                  listed_[static_cast<std::size_t>(index)].c_str());
  }
  PicoFileType getFileType(int index) override {
    return index >= 0 && static_cast<std::size_t>(index) < listed_.size()
               ? PFT_FILE
               : PFT_UNKNOWN;
  }
  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return false; }

  bool DeleteFile(const char *name) override {
    if (name == nullptr || failDelete_ == name)
      return false;
    return files_.erase(name) == 1U;
  }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *name) override {
    return name != nullptr && files_.contains(name);
  }
  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int) override { return 0U; }

  bool CopyFile(const char *source, const char *destination) override {
    if (source == nullptr || destination == nullptr || failCopy_ ||
        !files_.contains(source))
      return false;
    files_[destination] = files_[source];
    return true;
  }

  bool MoveFile(const char *source, const char *destination) override {
    if (source == nullptr || destination == nullptr ||
        (failMove_.first == source && failMove_.second == destination) ||
        (failMove2_.first == source && failMove2_.second == destination) ||
        !files_.contains(source) || files_.contains(destination))
      return false;
    files_[destination] = std::move(files_[source]);
    files_.erase(source);
    return true;
  }

  bool isExFat() override { return false; }

  void Put(const char *path, std::vector<std::uint8_t> bytes) {
    files_[path] = std::move(bytes);
  }
  const std::vector<std::uint8_t> &Bytes(const char *path) const {
    return files_.at(path);
  }
  void FailCopy(bool fail) { failCopy_ = fail; }
  void FailSyncAfter(std::uint32_t successfulCalls) {
    failSync_ = true;
    syncCalls_ = 0U;
    failSyncAfter_ = successfulCalls;
  }
  void FailMove(const char *source, const char *destination) {
    failMove_ = {source, destination};
  }
  void FailMoveAlso(const char *source, const char *destination) {
    failMove2_ = {source, destination};
  }
  void ClearFailures() {
    failCopy_ = false;
    failSync_ = false;
    syncCalls_ = 0U;
    failSyncAfter_ = 0U;
    failMove_ = {};
    failMove2_ = {};
    failDelete_.clear();
  }

private:
  FileSystem *previous_ = nullptr;
  std::map<std::string, std::vector<std::uint8_t>> files_{};
  std::vector<std::string> listed_{};
  std::pair<std::string, std::string> failMove_{};
  std::pair<std::string, std::string> failMove2_{};
  std::string failDelete_{};
  bool failCopy_ = false;
  bool failSync_ = false;
  std::uint32_t syncCalls_ = 0U;
  std::uint32_t failSyncAfter_ = 0U;
};

void AppendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (std::uint8_t shift = 0U; shift < 4U; ++shift)
    bytes.push_back(static_cast<std::uint8_t>(value >> (shift * 8U)));
}

std::vector<std::uint8_t> MakeWav() {
  std::vector<std::uint8_t> bytes;
  const auto appendFourCc = [&bytes](const char *value) {
    bytes.insert(bytes.end(), value, value + 4U);
  };
  appendFourCc("RIFF");
  AppendU32(bytes, 44U);
  appendFourCc("WAVE");
  appendFourCc("fmt ");
  AppendU32(bytes, 16U);
  AppendU16(bytes, 1U);
  AppendU16(bytes, 1U);
  AppendU32(bytes, 44100U);
  AppendU32(bytes, 88200U);
  AppendU16(bytes, 2U);
  AppendU16(bytes, 16U);
  appendFourCc("data");
  AppendU32(bytes, 8U);
  AppendU16(bytes, 100U);
  AppendU16(bytes, 200U);
  AppendU16(bytes, 300U);
  AppendU16(bytes, 400U);
  return bytes;
}

std::vector<std::uint8_t> MakeWavWithEncoding(std::uint16_t audioFormat,
                                              std::uint16_t bitsPerSample) {
  std::vector<std::uint8_t> bytes;
  const auto appendFourCc = [&bytes](const char *value) {
    bytes.insert(bytes.end(), value, value + 4U);
  };
  const std::uint16_t bytesPerSample =
      static_cast<std::uint16_t>(bitsPerSample / 8U);
  const std::uint32_t dataBytes = 4U * bytesPerSample;
  appendFourCc("RIFF");
  AppendU32(bytes, 36U + dataBytes);
  appendFourCc("WAVE");
  appendFourCc("fmt ");
  AppendU32(bytes, 16U);
  AppendU16(bytes, audioFormat);
  AppendU16(bytes, 1U);
  AppendU32(bytes, 44100U);
  AppendU32(bytes, 44100U * bytesPerSample);
  AppendU16(bytes, bytesPerSample);
  AppendU16(bytes, bitsPerSample);
  appendFourCc("data");
  AppendU32(bytes, dataBytes);
  bytes.resize(bytes.size() + dataBytes, 0x20U);
  return bytes;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

} // namespace

TEST_CASE("UI2 sample editor applies to a working copy and commits atomically") {
  using namespace ui2;
  constexpr const char *source = "/projects/DEMO/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;

  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  REQUIRE(transaction.ApplyTrim(1U, 3U) ==
          Ui2SampleEditorTransactionResult::Applied);
  CHECK(fileSystem.Bytes(source) == original);
  REQUIRE(fileSystem.exists(transaction.WorkingPath()));
  CHECK(ReadU32(fileSystem.Bytes(transaction.WorkingPath()), 40U) == 6U);
  CHECK(fileSystem.Bytes(transaction.WorkingPath())[44U] == 200U);

  REQUIRE(transaction.Save() == Ui2SampleEditorTransactionResult::Saved);
  CHECK_FALSE(transaction.Active());
  CHECK_FALSE(fileSystem.exists(transaction.WorkingPath()));
  CHECK_FALSE(fileSystem.exists(transaction.BackupPath()));
  CHECK(ReadU32(fileSystem.Bytes(source), 40U) == 6U);
  CHECK(fileSystem.Bytes(source)[44U] == 200U);
}

TEST_CASE("UI2 sample editor discard preserves the authoritative sample") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;

  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  REQUIRE(transaction.ApplyTrim(1U, 2U) ==
          Ui2SampleEditorTransactionResult::Applied);
  REQUIRE(transaction.Discard() ==
          Ui2SampleEditorTransactionResult::Discarded);
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(fileSystem.exists(transaction.WorkingPath()));
}

TEST_CASE("UI2 sample editor restores the original when promotion fails") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;

  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  REQUIRE(transaction.ApplyTrim(1U, 3U) ==
          Ui2SampleEditorTransactionResult::Applied);
  fileSystem.FailMove(transaction.WorkingPath(), source);
  CHECK(transaction.Save() == Ui2SampleEditorTransactionResult::SaveFailed);
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(fileSystem.exists(transaction.BackupPath()));
}

TEST_CASE("UI2 sample editor retry preserves the only backup generation") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;

  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  REQUIRE(transaction.ApplyTrim(1U, 3U) ==
          Ui2SampleEditorTransactionResult::Applied);
  const std::string working = transaction.WorkingPath();
  const std::string backup = transaction.BackupPath();
  fileSystem.FailMove(working.c_str(), source);
  fileSystem.FailMoveAlso(backup.c_str(), source);

  CHECK(transaction.Save() ==
        Ui2SampleEditorTransactionResult::RecoveryFailed);
  CHECK_FALSE(fileSystem.exists(source));
  REQUIRE(fileSystem.exists(backup.c_str()));
  CHECK(fileSystem.Bytes(backup.c_str()) == original);
  REQUIRE(fileSystem.exists(working.c_str()));

  // A direct retry must attempt recovery first and must not delete the only
  // authoritative generation merely because its backup filename exists.
  CHECK(transaction.Save() ==
        Ui2SampleEditorTransactionResult::RecoveryFailed);
  CHECK_FALSE(fileSystem.exists(source));
  REQUIRE(fileSystem.exists(backup.c_str()));
  CHECK(fileSystem.Bytes(backup.c_str()) == original);

  fileSystem.ClearFailures();
  Ui2SampleEditorTransaction reopened;
  REQUIRE(reopened.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(fileSystem.exists(backup.c_str()));
  CHECK_FALSE(fileSystem.exists(working.c_str()));
}

TEST_CASE("UI2 sample editor recovers an interrupted backup before opening") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  Ui2SampleEditorTransaction paths;
  fileSystem.Put(source, MakeWav());
  REQUIRE(paths.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  const std::string backup = paths.BackupPath();
  REQUIRE(paths.Discard() == Ui2SampleEditorTransactionResult::Discarded);
  REQUIRE(fileSystem.MoveFile(source, backup.c_str()));

  Ui2SampleEditorTransaction recovered;
  CHECK(recovered.Begin(fileSystem, source) ==
        Ui2SampleEditorTransactionResult::Ready);
  CHECK(fileSystem.exists(source));
  CHECK_FALSE(fileSystem.exists(backup.c_str()));
}

TEST_CASE("UI2 sample editor journals are isolated per sample") {
  using namespace ui2;
  constexpr const char *sampleA = "/samples/A.wav";
  constexpr const char *sampleB = "/samples/B.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> originalA = MakeWav();
  fileSystem.Put(sampleA, originalA);
  fileSystem.Put(sampleB, MakeWav());

  Ui2SampleEditorTransaction interruptedA;
  REQUIRE(interruptedA.Begin(fileSystem, sampleA) ==
          Ui2SampleEditorTransactionResult::Ready);
  const std::string backupA = interruptedA.BackupPath();
  const std::string workingA = interruptedA.WorkingPath();
  REQUIRE(interruptedA.Discard() ==
          Ui2SampleEditorTransactionResult::Discarded);
  REQUIRE(fileSystem.MoveFile(sampleA, backupA.c_str()));

  Ui2SampleEditorTransaction transactionB;
  REQUIRE(transactionB.Begin(fileSystem, sampleB) ==
          Ui2SampleEditorTransactionResult::Ready);
  CHECK(std::strcmp(transactionB.BackupPath(), backupA.c_str()) != 0);
  CHECK(std::strcmp(transactionB.WorkingPath(), workingA.c_str()) != 0);
  CHECK(fileSystem.exists(backupA.c_str()));
  CHECK_FALSE(fileSystem.exists(sampleA));

  Ui2SampleEditorTransaction recoveredA;
  REQUIRE(recoveredA.Begin(fileSystem, sampleA) ==
          Ui2SampleEditorTransactionResult::Ready);
  CHECK(fileSystem.Bytes(sampleA) == originalA);
  CHECK_FALSE(fileSystem.exists(backupA.c_str()));
}

TEST_CASE("UI2 sample editor copy failure never mutates the source") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;
  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  fileSystem.FailCopy(true);

  CHECK(transaction.ApplyTrim(1U, 3U) ==
        Ui2SampleEditorTransactionResult::CopyFailed);
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(transaction.HasWorkingCopy());
}

TEST_CASE("UI2 sample editor journal supports long FAT-safe names") {
  using namespace ui2;
  const std::string source =
      std::string("/samples/") + std::string(180U, 'L') + ".Wav";
  SampleEditMemoryFileSystem fileSystem;
  fileSystem.Put(source.c_str(), MakeWav());
  Ui2SampleEditorTransaction transaction;

  REQUIRE(transaction.Begin(fileSystem, source.c_str()) ==
          Ui2SampleEditorTransactionResult::Ready);
  CHECK(std::strlen(transaction.WorkingPath()) == source.size());
  CHECK(std::strlen(transaction.BackupPath()) == source.size());
  CHECK(std::strstr(transaction.WorkingPath(), ".w1") != nullptr);
  CHECK(std::strstr(transaction.BackupPath(), ".b1") != nullptr);
  CHECK(transaction.ApplyTrim(1U, 3U) ==
        Ui2SampleEditorTransactionResult::Applied);
  CHECK(fileSystem.exists(transaction.WorkingPath()));
}

TEST_CASE("UI2 sample editor backup path reversibly preserves extension case") {
  for (const char *source : {"/samples/a.wav", "/samples/a.WAV",
                             "/samples/a.WaV", "/samples/a.wAv"}) {
    char backup[PFILENAME_SIZE]{};
    char decoded[PFILENAME_SIZE]{};
    REQUIRE(SampleEditorFileJournal::BuildPath(
        source, SampleEditorFileJournal::Generation::Backup, backup,
        sizeof(backup)));
    REQUIRE(SampleEditorFileJournal::DecodeBackupPath(backup, decoded,
                                                      sizeof(decoded)));
    CHECK(std::strcmp(decoded, source) == 0);
  }
}

TEST_CASE("sample pool load restores a backup-only generation") {
  using namespace ui2;
  constexpr const char *source = "VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction interrupted;
  REQUIRE(interrupted.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  REQUIRE(interrupted.ApplyTrim(1U, 3U) ==
          Ui2SampleEditorTransactionResult::Applied);
  const std::string working = interrupted.WorkingPath();
  const std::string backup = interrupted.BackupPath();
  REQUIRE(fileSystem.MoveFile(source, backup.c_str()));
  REQUIRE(fileSystem.exists(working.c_str()));
  REQUIRE_FALSE(fileSystem.exists(source));

  etl::vector<int, 8> loaded;
  REQUIRE(SamplePoolLoading::EnterAndList(fileSystem, "DEMO", loaded));
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(fileSystem.exists(backup.c_str()));
  CHECK_FALSE(fileSystem.exists(working.c_str()));
}

TEST_CASE("sample directory scan removes an interrupted working-only copy") {
  constexpr const char *source = "VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  char working[PFILENAME_SIZE]{};
  REQUIRE(SampleEditorFileJournal::BuildPath(
      source, SampleEditorFileJournal::Generation::Working, working,
      sizeof(working)));
  fileSystem.Put(working, {0x52U, 0x49U});

  REQUIRE(SampleEditorFileJournal::RecoverCurrentDirectory(fileSystem));
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(fileSystem.exists(working));
}

TEST_CASE("UI2 sample normalize rejects encodings it cannot transform") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";

  for (const auto [audioFormat, bitsPerSample] :
       {std::pair<std::uint16_t, std::uint16_t>{1U, 24U},
        {1U, 32U}, {3U, 32U}, {3U, 64U}}) {
    CAPTURE(audioFormat);
    CAPTURE(bitsPerSample);
    SampleEditMemoryFileSystem fileSystem;
    const std::vector<std::uint8_t> original =
        MakeWavWithEncoding(audioFormat, bitsPerSample);
    fileSystem.Put(source, original);
    Ui2SampleEditorTransaction transaction;
    REQUIRE(transaction.Begin(fileSystem, source) ==
            Ui2SampleEditorTransactionResult::Ready);

    CHECK(transaction.ApplyNormalize() ==
          Ui2SampleEditorTransactionResult::MutationFailed);
    CHECK(fileSystem.Bytes(source) == original);
    CHECK_FALSE(transaction.HasWorkingCopy());
  }
}

TEST_CASE("UI2 sample normalize propagates durable sync failure") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;
  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  // The first durable flush commits the temporary working copy. Fail the
  // following normalization flush specifically.
  fileSystem.FailSyncAfter(1U);

  CHECK(transaction.ApplyNormalize() ==
        Ui2SampleEditorTransactionResult::MutationFailed);
  CHECK(fileSystem.Bytes(source) == original);
  CHECK_FALSE(transaction.HasWorkingCopy());
}

TEST_CASE("UI2 sample trim no-op removes its transient working copy") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  const std::vector<std::uint8_t> original = MakeWav();
  fileSystem.Put(source, original);
  Ui2SampleEditorTransaction transaction;
  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);

  CHECK(transaction.ApplyTrim(0U, 3U) ==
        Ui2SampleEditorTransactionResult::NoChanges);
  CHECK_FALSE(transaction.HasWorkingCopy());
  CHECK_FALSE(fileSystem.exists(transaction.WorkingPath()));
  CHECK(fileSystem.Bytes(source) == original);
}

TEST_CASE("UI2 sample normalize no-op removes its transient working copy") {
  using namespace ui2;
  constexpr const char *source = "/samples/SILENT.wav";
  SampleEditMemoryFileSystem fileSystem;
  std::vector<std::uint8_t> silent = MakeWav();
  std::fill(silent.begin() + 44, silent.end(), 0U);
  fileSystem.Put(source, silent);
  Ui2SampleEditorTransaction transaction;
  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);

  CHECK(transaction.ApplyNormalize() ==
        Ui2SampleEditorTransactionResult::NoChanges);
  CHECK_FALSE(transaction.HasWorkingCopy());
  CHECK_FALSE(fileSystem.exists(transaction.WorkingPath()));
  CHECK(fileSystem.Bytes(source) == silent);
}

TEST_CASE("UI2 sample no-op preserves an earlier unsaved edit") {
  using namespace ui2;
  constexpr const char *source = "/samples/VOICE.wav";
  SampleEditMemoryFileSystem fileSystem;
  fileSystem.Put(source, MakeWav());
  Ui2SampleEditorTransaction transaction;
  REQUIRE(transaction.Begin(fileSystem, source) ==
          Ui2SampleEditorTransactionResult::Ready);
  REQUIRE(transaction.ApplyTrim(1U, 3U) ==
          Ui2SampleEditorTransactionResult::Applied);
  const std::vector<std::uint8_t> edited =
      fileSystem.Bytes(transaction.WorkingPath());

  CHECK(transaction.ApplyTrim(0U, 2U) ==
        Ui2SampleEditorTransactionResult::NoChanges);
  CHECK(transaction.HasWorkingCopy());
  CHECK(fileSystem.Bytes(transaction.WorkingPath()) == edited);
}
