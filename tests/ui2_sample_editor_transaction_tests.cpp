#include "doctest/doctest.h"

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
  SampleEditMemoryFile(std::vector<std::uint8_t> &bytes, bool writable)
      : bytes_(bytes), writable_(writable) {}

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
  bool Sync() override { return true; }
  void Dispose() override { delete this; }

protected:
  bool Close() override { return true; }

private:
  std::vector<std::uint8_t> &bytes_;
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
    const auto found = files_.find(name);
    if (found == files_.end())
      return {};
    const bool writable = std::strchr(mode, '+') != nullptr ||
                          std::strchr(mode, 'w') != nullptr;
    return MakeFileHandle(new SampleEditMemoryFile(found->second, writable));
  }

  bool chdir(const char *) override { return false; }
  void list(etl::ivector<int> *, const char *, bool, bool = false) override {}
  void getFileName(int, char *, int) override {}
  PicoFileType getFileType(int) override { return PFT_UNKNOWN; }
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
  void FailMove(const char *source, const char *destination) {
    failMove_ = {source, destination};
  }
  void ClearFailures() {
    failCopy_ = false;
    failMove_ = {};
    failDelete_.clear();
  }

private:
  FileSystem *previous_ = nullptr;
  std::map<std::string, std::vector<std::uint8_t>> files_{};
  std::pair<std::string, std::string> failMove_{};
  std::string failDelete_{};
  bool failCopy_ = false;
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
