#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// TinyXML's embedded adapter remaps stdio identifiers. Keep all host standard
// library headers above the production persistence headers.
#include "../sources/Application/Model/Config.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"

namespace {

class ThemeMemoryFileSystem;

class ThemeMemoryFile final : public I_File {
public:
  ThemeMemoryFile(ThemeMemoryFileSystem &fileSystem, std::string path,
                  bool readable, bool writable);
  ~ThemeMemoryFile() override;

  int Read(void *ptr, int size) override;
  int GetC() override;
  int Write(const void *ptr, int size, int count) override;
  void Seek(long offset, int whence) override;
  long Tell() override { return static_cast<long>(cursor_); }
  int Error() override { return 0; }
  bool Sync() override;
  void Dispose() override;

protected:
  bool Close() override;

private:
  ThemeMemoryFileSystem &fileSystem_;
  std::string path_;
  std::size_t cursor_ = 0U;
  bool readable_ = false;
  bool writable_ = false;
  bool closed_ = false;
};

class ThemeMemoryFileSystem final : public FileSystem {
public:
  ThemeMemoryFileSystem() { directories_.insert("/"); }

  FileHandle Open(const char *name, const char *mode) override {
    if (name == nullptr || mode == nullptr)
      return {};
    const std::string path = Normalize(name);
    const bool readable = std::strchr(mode, 'r') != nullptr;
    const bool writable = std::strchr(mode, 'w') != nullptr;
    if (readable && files_.find(path) == files_.end())
      return {};
    if (writable)
      files_[path].clear();
    if (!readable && !writable)
      return {};
    return MakeFileHandle(
        new ThemeMemoryFile(*this, path, readable, writable));
  }

  bool chdir(const char *path) override {
    return path != nullptr && directories_.contains(Normalize(path));
  }
  bool read(int, void *) override { return false; }
  void list(etl::ivector<int> *indices, const char *, bool,
            bool = false) override {
    if (indices != nullptr)
      indices->clear();
  }
  void getFileName(int, char *name, int length) override {
    if (name != nullptr && length > 0)
      name[0] = '\0';
  }
  PicoFileType getFileType(int) override { return PFT_UNKNOWN; }
  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return true; }
  bool DeleteFile(const char *name) override {
    return name != nullptr && files_.erase(Normalize(name)) != 0U;
  }
  bool DeleteDir(const char *name) override {
    return name != nullptr && directories_.erase(Normalize(name)) != 0U;
  }
  bool exists(const char *path) override {
    if (path == nullptr)
      return false;
    const std::string normalized = Normalize(path);
    return files_.contains(normalized) || directories_.contains(normalized);
  }
  bool makeDir(const char *path, bool = false) override {
    return path != nullptr && directories_.insert(Normalize(path)).second;
  }
  std::uint64_t getFileSize(int) override { return 0U; }
  bool CopyFile(const char *source, const char *target) override {
    if (source == nullptr || target == nullptr)
      return false;
    const auto found = files_.find(Normalize(source));
    if (found == files_.end())
      return false;
    files_[Normalize(target)] = found->second;
    return true;
  }
  bool MoveFile(const char *source, const char *target) override {
    if (source == nullptr || target == nullptr)
      return false;
    const std::string sourcePath = Normalize(source);
    const std::string targetPath = Normalize(target);
    if (failMoveCount_ != 0U && failMoveTarget_ == targetPath) {
      --failMoveCount_;
      return false;
    }
    const auto found = files_.find(sourcePath);
    if (found == files_.end())
      return false;
    files_[targetPath] = found->second;
    files_.erase(found);
    return true;
  }
  bool isExFat() override { return false; }

  void MakeDirectory(const char *path) { directories_.insert(Normalize(path)); }

  void Put(const char *path, const std::string &contents) {
    files_[Normalize(path)] =
        std::vector<std::uint8_t>(contents.begin(), contents.end());
  }

  [[nodiscard]] std::string Get(const char *path) const {
    const auto found = files_.find(Normalize(path));
    if (found == files_.end())
      return {};
    return {found->second.begin(), found->second.end()};
  }

  void LimitWrites(const char *path, std::size_t bytes) {
    limitedWritePath_ = Normalize(path);
    remainingWriteBytes_ = bytes;
    limitWrites_ = true;
  }
  void FailNextSync(const char *path) {
    failSyncPath_ = Normalize(path);
    failSyncCount_ = 1U;
  }
  void FailNextClose(const char *path) {
    failClosePath_ = Normalize(path);
    failCloseCount_ = 1U;
  }
  void FailNextMoveTo(const char *path) {
    failMoveTarget_ = Normalize(path);
    failMoveCount_ = 1U;
  }

private:
  friend class ThemeMemoryFile;

  static std::string Normalize(const char *path) {
    if (path == nullptr || path[0] == '\0')
      return {};
    return path[0] == '/' ? std::string(path) : std::string("/") + path;
  }

  std::size_t AllowedWrite(const std::string &path, std::size_t requested) {
    if (!limitWrites_ || path != limitedWritePath_)
      return requested;
    const std::size_t allowed = std::min(requested, remainingWriteBytes_);
    remainingWriteBytes_ -= allowed;
    return allowed;
  }

  bool Sync(const std::string &path) {
    if (failSyncCount_ != 0U && failSyncPath_ == path) {
      --failSyncCount_;
      return false;
    }
    return true;
  }

  bool Close(const std::string &path) {
    if (failCloseCount_ != 0U && failClosePath_ == path) {
      --failCloseCount_;
      return false;
    }
    return true;
  }

  std::map<std::string, std::vector<std::uint8_t>> files_;
  std::set<std::string> directories_;
  std::string limitedWritePath_;
  std::string failSyncPath_;
  std::string failClosePath_;
  std::string failMoveTarget_;
  std::size_t remainingWriteBytes_ = 0U;
  std::uint8_t failSyncCount_ = 0U;
  std::uint8_t failCloseCount_ = 0U;
  std::uint8_t failMoveCount_ = 0U;
  bool limitWrites_ = false;
};

ThemeMemoryFile::ThemeMemoryFile(ThemeMemoryFileSystem &fileSystem,
                                 std::string path, bool readable,
                                 bool writable)
    : fileSystem_(fileSystem), path_(std::move(path)), readable_(readable),
      writable_(writable) {}

ThemeMemoryFile::~ThemeMemoryFile() {
  if (!closed_)
    (void)Close();
}

int ThemeMemoryFile::Read(void *ptr, int size) {
  if (closed_ || !readable_ || ptr == nullptr || size <= 0)
    return 0;
  const auto found = fileSystem_.files_.find(path_);
  if (found == fileSystem_.files_.end() || cursor_ >= found->second.size())
    return 0;
  const std::size_t count = std::min<std::size_t>(
      static_cast<std::size_t>(size), found->second.size() - cursor_);
  std::memcpy(ptr, found->second.data() + cursor_, count);
  cursor_ += count;
  return static_cast<int>(count);
}

int ThemeMemoryFile::GetC() {
  std::uint8_t byte = 0U;
  return Read(&byte, 1) == 1 ? static_cast<int>(byte) : EOF;
}

int ThemeMemoryFile::Write(const void *ptr, int size, int count) {
  if (closed_ || !writable_ || ptr == nullptr || size <= 0 || count <= 0)
    return 0;
  const std::size_t requested = static_cast<std::size_t>(size) *
                                static_cast<std::size_t>(count);
  const std::size_t allowed = fileSystem_.AllowedWrite(path_, requested);
  auto &bytes = fileSystem_.files_[path_];
  if (cursor_ + allowed > bytes.size())
    bytes.resize(cursor_ + allowed);
  std::memcpy(bytes.data() + cursor_, ptr, allowed);
  cursor_ += allowed;
  return static_cast<int>(allowed / static_cast<std::size_t>(size));
}

void ThemeMemoryFile::Seek(long offset, int whence) {
  std::size_t base = 0U;
  if (whence == SEEK_CUR)
    base = cursor_;
  else if (whence == SEEK_END)
    base = fileSystem_.files_[path_].size();
  const long long target = static_cast<long long>(base) + offset;
  cursor_ = target < 0 ? 0U : static_cast<std::size_t>(target);
}

bool ThemeMemoryFile::Sync() {
  return !closed_ && fileSystem_.Sync(path_);
}

bool ThemeMemoryFile::Close() {
  if (closed_)
    return true;
  closed_ = true;
  return fileSystem_.Close(path_);
}

void ThemeMemoryFile::Dispose() { delete this; }

class ThemeFixture {
public:
  ThemeFixture() : previous_(FileSystem::GetInstance()) {
    fileSystem_.MakeDirectory("/themes");
    FileSystem::Install(&fileSystem_);
  }
  ~ThemeFixture() { FileSystem::Install(previous_); }

  ThemeMemoryFileSystem fileSystem_;

private:
  FileSystem *previous_ = nullptr;
};

constexpr std::array<FourCC::enum_type, 12U> kLegacyColorIds{{
    FourCC::VarBGColor,        FourCC::VarFGColor,
    FourCC::VarHI1Color,       FourCC::VarHI2Color,
    FourCC::VarConsoleColor,   FourCC::VarCursorColor,
    FourCC::VarInfoColor,      FourCC::VarWarnColor,
    FourCC::VarErrorColor,     FourCC::VarAccentColor,
    FourCC::VarAccentAltColor, FourCC::VarEmphasisColor,
}};

constexpr std::array<const char *, Config::SemanticThemeColorCount>
    kSemanticColorNames{{
        "surface.bg",       "surface.top_bar", "surface.bottom_bar",
        "text.normal",      "text.dim",        "text.highlighted",
        "text.colored",     "cursor.primary",  "cursor.row",
        "playback.active",  "system.info",     "system.warning",
        "system.error",     "battery.normal",  "battery.charging",
        "battery.low",      "vu.safe",         "vu.warning",
        "vu.peak",
}};

std::string NptThemeXml(bool closeRoot = true) {
  std::string xml =
      "<NPT MAGIC=\"NPT\" VERSION=\"1\"><Font value=\"2\"/>";
  for (std::size_t index = 0U; index < kSemanticColorNames.size(); ++index) {
    char color[8]{};
    std::snprintf(color, sizeof(color), "#%06X",
                  static_cast<unsigned int>(0x100U + index));
    xml += "<UiColor key=\"";
    xml += kSemanticColorNames[index];
    xml += "\" value=\"";
    xml += color;
    xml += "\"/>";
  }
  if (closeRoot)
    xml += "</NPT>";
  return xml;
}

struct LiveThemeSnapshot {
  std::array<int, kLegacyColorIds.size()> legacy{};
  Config::SemanticThemeColors semantic{};
  int font = 0;
  std::string name;
};

LiveThemeSnapshot Capture(Config &config) {
  LiveThemeSnapshot snapshot;
  for (std::size_t index = 0U; index < snapshot.legacy.size(); ++index) {
    Variable *value = config.FindVariable(kLegacyColorIds[index]);
    REQUIRE(value != nullptr);
    snapshot.legacy[index] = value->GetInt();
  }
  Variable *font = config.FindVariable(FourCC::VarUIFont);
  Variable *name = config.FindVariable(FourCC::VarThemeName);
  REQUIRE(font != nullptr);
  REQUIRE(name != nullptr);
  snapshot.font = font->GetInt();
  snapshot.name = name->GetString().c_str();
  snapshot.semantic = config.GetSemanticThemeColors();
  return snapshot;
}

void CheckSame(const LiveThemeSnapshot &expected, Config &config) {
  const LiveThemeSnapshot actual = Capture(config);
  CHECK(actual.legacy == expected.legacy);
  CHECK(actual.semantic == expected.semantic);
  CHECK(actual.font == expected.font);
  CHECK(actual.name == expected.name);
}

void SetDistinctTheme(Config &config, std::uint32_t base) {
  for (std::size_t index = 0U; index < kLegacyColorIds.size(); ++index) {
    Variable *color = config.FindVariable(kLegacyColorIds[index]);
    REQUIRE(color != nullptr);
    color->SetInt(static_cast<int>(base + index));
  }
  Config::SemanticThemeColors semantic{};
  for (std::size_t index = 0U; index < semantic.size(); ++index)
    semantic[index] = (base + 0x100U + index) & 0x00FFFFFFU;
  config.SetSemanticThemeColors(semantic);
}

} // namespace

TEST_CASE("theme import validates the complete stream before changing Config") {
  ThemeFixture fixture;
  Config config;
  SetDistinctTheme(config, 0x220000U);
  config.FindVariable(FourCC::VarUIFont)->SetInt(1);
  config.FindVariable(FourCC::VarThemeName)->SetString("ORIGINAL");
  const LiveThemeSnapshot original = Capture(config);
  fixture.fileSystem_.Put("/.config.xml", "CONFIG-SENTINEL");

  std::string illegalAttribute = NptThemeXml();
  illegalAttribute.insert(illegalAttribute.find("value=\"2\"") + 9U,
                          " rogue=\"1\"");
  std::string missingAttribute = NptThemeXml();
  const std::size_t value = missingAttribute.find(" value=\"#000100\"");
  REQUIRE(value != std::string::npos);
  missingAttribute.erase(value, std::strlen(" value=\"#000100\""));
  const std::array<std::pair<const char *, std::string>, 4U> invalid{{
      {"TRUNCATED", NptThemeXml(false)},
      {"ILLEGAL", illegalAttribute},
      {"MISSING", missingAttribute},
      {"OVERLONG",
       "<NPT MAGIC=\"NPT\" VERSION=\"1\"><Font value=\"2\"/><UiColor key=\"" +
           std::string(80U, 'A') + "\" value=\"#000001\"/></NPT>"},
  }};

  for (const auto &[name, xml] : invalid) {
    CAPTURE(name);
    fixture.fileSystem_.Put((std::string("/themes/") + name + ".npt").c_str(),
                            xml);
    bool loaded = true;
    CHECK_FALSE(config.ImportTheme(name, &loaded));
    CHECK_FALSE(loaded);
    CheckSame(original, config);
    CHECK(fixture.fileSystem_.Get("/.config.xml") == "CONFIG-SENTINEL");
  }
}

TEST_CASE("ptt themes are rejected without changing live state") {
  ThemeFixture fixture;
  Config config;
  const LiveThemeSnapshot original = Capture(config);
  fixture.fileSystem_.Put("/themes/LEGACY.ptt", "<THEME/>");

  CHECK_FALSE(config.ImportTheme("LEGACY.ptt"));
  CheckSame(original, config);

  // File suffix is only a browser filter: content magic rejects a renamed PTT.
  fixture.fileSystem_.Put("/themes/RENAMED.npt", "<THEME/>");
  CHECK_FALSE(config.ImportTheme("RENAMED.npt"));
  CheckSame(original, config);
}

TEST_CASE("old device config is discarded instead of migrated") {
  ThemeFixture fixture;
  fixture.fileSystem_.Put(
      "/.config.xml",
      "<CONFIG VERSION=\"3\">"
      "<OUTPUTVOLUME VALUE=\"77\"/>"
      "<Color name=\"BACKGROUND\" value=\"#000000\"/>"
      "<Color name=\"FOREGROUND\" value=\"#FFFFFF\"/>"
      "<UiColor key=\"surface.bg\" value=\"#000000\"/>"
      "<UiColor key=\"surface.top_bar\" value=\"#000000\"/>"
      "<UiColor key=\"text.colored\" value=\"#32ECFF\"/>"
      "</CONFIG>");

  Config config;
  CHECK(config.FindVariable(FourCC::VarOutputVolume)->GetInt() == 40);
  CHECK(config.GetSemanticThemeColors() ==
        Config::DefaultSemanticThemeColors());

  const std::string replaced = fixture.fileSystem_.Get("/.config.xml");
  CHECK(replaced.find("VERSION=\"4\"") != std::string::npos);
  CHECK(replaced.find("key=\"surface.bg\" value=\"#030707\"") !=
        std::string::npos);
  CHECK(replaced.find("key=\"text.colored\" value=\"#45DCE8\"") !=
        std::string::npos);
}

TEST_CASE("incomplete current device config is discarded instead of backfilled") {
  ThemeFixture fixture;
  fixture.fileSystem_.Put(
      "/.config.xml",
      "<CONFIG VERSION=\"4\">"
      "<UiColor key=\"surface.bg\" value=\"#123456\"/>"
      "</CONFIG>");

  Config config;
  CHECK(config.FindVariable(FourCC::VarOutputVolume)->GetInt() == 40);
  CHECK(config.GetSemanticThemeColors() ==
        Config::DefaultSemanticThemeColors());

  const std::string replaced = fixture.fileSystem_.Get("/.config.xml");
  CHECK(replaced.find("key=\"surface.bg\" value=\"#030707\"") !=
        std::string::npos);
  CHECK(replaced.find("key=\"text.colored\" value=\"#45DCE8\"") !=
        std::string::npos);
}

TEST_CASE("complete current device config loads without migration") {
  ThemeFixture fixture;
  Config::SemanticThemeColors expected = Config::DefaultSemanticThemeColors();
  expected[0] = 0x123456U;
  expected[6] = 0x654321U;
  {
    Config config;
    config.FindVariable(FourCC::VarOutputVolume)->SetInt(77);
    config.SetSemanticThemeColors(expected);
    REQUIRE(config.Save());
  }

  const std::string saved = fixture.fileSystem_.Get("/.config.xml");
  CHECK(saved.find("REMOTEUI") == std::string::npos);
  Config loaded;
  CHECK(loaded.FindVariable(FourCC::VarOutputVolume)->GetInt() == 77);
  CHECK(loaded.GetSemanticThemeColors() == expected);
  CHECK(fixture.fileSystem_.Get("/.config.xml") == saved);
}

TEST_CASE("theme overwrite preserves old bytes on short write and move failure") {
  {
    ThemeFixture fixture;
    Config config;
    SetDistinctTheme(config, 0x110000U);
    REQUIRE(config.ExportTheme("SAFE", false));
    const std::string oldTheme = fixture.fileSystem_.Get("/themes/SAFE.npt");
    REQUIRE_FALSE(oldTheme.empty());

    SetDistinctTheme(config, 0x330000U);
    fixture.fileSystem_.LimitWrites("/themes/.SAFE.npt.tmp", 80U);
    CHECK_FALSE(config.ExportTheme("SAFE", true));
    CHECK(fixture.fileSystem_.Get("/themes/SAFE.npt") == oldTheme);
    CHECK_FALSE(fixture.fileSystem_.exists("/themes/.SAFE.npt.tmp"));
    CHECK_FALSE(fixture.fileSystem_.exists("/themes/.SAFE.npt.bak"));
  }

  // A fresh fixture removes the one-shot write limiter while retaining a
  // separate old-theme rollback assertion for the install rename.
  {
    ThemeFixture renameFixture;
    Config renameConfig;
    SetDistinctTheme(renameConfig, 0x440000U);
    REQUIRE(renameConfig.ExportTheme("SAFE", false));
    const std::string renameOld =
        renameFixture.fileSystem_.Get("/themes/SAFE.npt");
    SetDistinctTheme(renameConfig, 0x550000U);
    renameFixture.fileSystem_.FailNextMoveTo("/themes/SAFE.npt");
    CHECK_FALSE(renameConfig.ExportTheme("SAFE", true));
    CHECK(renameFixture.fileSystem_.Get("/themes/SAFE.npt") == renameOld);
    CHECK_FALSE(renameFixture.fileSystem_.exists("/themes/.SAFE.npt.tmp"));
    CHECK_FALSE(renameFixture.fileSystem_.exists("/themes/.SAFE.npt.bak"));
  }
}

TEST_CASE("theme export closes, syncs and round-trips all nineteen roles") {
  ThemeFixture fixture;
  Config config;
  SetDistinctTheme(config, 0x660000U);
  config.FindVariable(FourCC::VarUIFont)->SetInt(2);
  const LiveThemeSnapshot exported = Capture(config);

  REQUIRE(config.ExportTheme("ROUNDTRIP", false));
  CHECK_FALSE(fixture.fileSystem_.exists("/themes/.ROUNDTRIP.npt.tmp"));
  CHECK_FALSE(fixture.fileSystem_.exists("/themes/.ROUNDTRIP.npt.bak"));

  SetDistinctTheme(config, 0x770000U);
  config.FindVariable(FourCC::VarUIFont)->SetInt(0);
  const LiveThemeSnapshot beforeImport = Capture(config);
  REQUIRE(config.ImportTheme("ROUNDTRIP.npt"));
  const LiveThemeSnapshot restored = Capture(config);
  // NPT is a UI2-only format; source-only UI1 colors are not serialized.
  CHECK(restored.legacy == beforeImport.legacy);
  CHECK(restored.semantic == exported.semantic);
  CHECK(restored.font == exported.font);
  CHECK(restored.name == "ROUNDTRIP");
}

TEST_CASE("theme export rejects sync and close failures without touching target") {
  ThemeFixture fixture;
  Config config;
  SetDistinctTheme(config, 0x120000U);
  REQUIRE(config.ExportTheme("DURABLE", false));
  const std::string original = fixture.fileSystem_.Get("/themes/DURABLE.npt");

  SetDistinctTheme(config, 0x130000U);
  fixture.fileSystem_.FailNextSync("/themes/.DURABLE.npt.tmp");
  CHECK_FALSE(config.ExportTheme("DURABLE", true));
  CHECK(fixture.fileSystem_.Get("/themes/DURABLE.npt") == original);

  fixture.fileSystem_.FailNextClose("/themes/.DURABLE.npt.tmp");
  CHECK_FALSE(config.ExportTheme("DURABLE", true));
  CHECK(fixture.fileSystem_.Get("/themes/DURABLE.npt") == original);
}

TEST_CASE("theme import recovers an interrupted overwrite from its old backup") {
  ThemeFixture fixture;
  Config config;
  SetDistinctTheme(config, 0x240000U);
  REQUIRE(config.ExportTheme("RECOVER", false));
  const std::string original = fixture.fileSystem_.Get("/themes/RECOVER.npt");
  REQUIRE(fixture.fileSystem_.MoveFile("/themes/RECOVER.npt",
                                       "/themes/.RECOVER.npt.bak"));
  fixture.fileSystem_.Put("/themes/.RECOVER.npt.tmp", "<NPT>");

  REQUIRE(config.ImportTheme("RECOVER"));
  CHECK(fixture.fileSystem_.Get("/themes/RECOVER.npt") == original);
  CHECK_FALSE(fixture.fileSystem_.exists("/themes/.RECOVER.npt.tmp"));
  CHECK_FALSE(fixture.fileSystem_.exists("/themes/.RECOVER.npt.bak"));
}
