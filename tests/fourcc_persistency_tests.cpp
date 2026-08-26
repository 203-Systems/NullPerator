#include "doctest/doctest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>

// TinyXML's embedded adapter intentionally remaps stdio names. Keep all host
// standard-library headers above the production persistence headers.
#include "Adapters/wasm/filesystem/WasmFileSystem.h"
#include "Application/Persistency/InstrumentFileValidator.h"
#include "Application/Instruments/SampleInstrumentParameterLimits.h"
#include "Application/Instruments/InstrumentBankRestorePolicy.h"
#include "Application/Persistency/PersistencyAttribute.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Persistency/Persistent.h"
#include "Application/Session/TrackerProjectLoadPolicy.h"
#include "Application/Model/Groove.h"
#include "Application/Model/Phrase.h"
#include "Application/Model/ProjectParameterRestore.h"
#include "Application/Model/Song.h"
#include "Application/Model/Table.h"
#include "Application/Utils/HexBuffers.h"
#include "Application/Utils/HelpLegend.h"

struct PersistencyServiceTestPeer {
  static PersistencyResult SaveStaging(PersistencyService &service) {
    return service.Save_(UNNAMED_PROJECT_NAME, "", false, true);
  }

  static PersistencyResult SaveStagingState(PersistencyService &service) {
    return service.SaveProjectState_(UNNAMED_PROJECT_NAME, true);
  }

  static PersistencyResult
  SaveStagingTransactionState(PersistencyService &service,
                              const char *previousProjectName) {
    return service.SaveProjectState_(UNNAMED_PROJECT_NAME, true, true,
                                     previousProjectName);
  }

  static PersistencyResult LoadBase(PersistencyService &service,
                                    const char *projectName) {
    return service.LoadBase_(projectName, false);
  }

  static PersistencyResult LoadJournalBackup(PersistencyService &service,
                                              const char *projectName,
                                              bool autosave) {
    return service.LoadProjectJournalBackup_(
        projectName, autosave,
        std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0);
  }

  static bool PromoteJournalBackup(PersistencyService &service,
                                   const char *projectName, bool autosave) {
    return service.PromoteProjectJournalBackup_(
        projectName, autosave,
        std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0);
  }

  static bool FinalizeJournal(PersistencyService &service,
                              const char *projectName, bool autosave) {
    return service.FinalizeProjectJournal_(
        projectName, autosave,
        std::strcmp(projectName, UNNAMED_PROJECT_NAME) == 0);
  }

  static PersistencyResult LoadStaging(PersistencyService &service) {
    return service.Load_(UNNAMED_PROJECT_NAME, true);
  }

  static bool ReadPreviousProject(PersistencyService &service,
                                  char *projectName) {
    return service.ReadPreviousProjectName_(projectName);
  }

  static bool BeginStagingReplacement(PersistencyService &service,
                                      bool &hadPrevious,
                                      const char *previousProjectName =
                                          UNNAMED_PROJECT_NAME) {
    return service.BeginStagingProjectReplacement_(previousProjectName,
                                                   hadPrevious);
  }

  static bool CommitStagingReplacement(PersistencyService &service,
                                       bool hadPrevious) {
    return service.CommitStagingProjectReplacement_(hadPrevious);
  }

  static bool RollbackStagingReplacement(PersistencyService &service,
                                         bool hadPrevious) {
    return service.RollbackStagingProjectReplacement_(hadPrevious);
  }

  static bool FinalizeCommittedStagingReplacement(
      PersistencyService &service) {
    return service.FinalizeCommittedStagingProjectReplacement_();
  }

  static bool RollbackCommittedStagingReplacement(
      PersistencyService &service, char *previousProjectName) {
    return service.RollbackCommittedStagingProjectReplacement_(
        previousProjectName);
  }
};

namespace {

class FourCCXmlFixture {
public:
  FourCCXmlFixture() : root_(MakeRoot()), filesystem_(root_.string()) {
    std::filesystem::create_directories(root_);
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(&filesystem_);
  }

  static std::filesystem::path MakeRoot() {
    static std::uint32_t sequence = 0;
    return std::filesystem::temp_directory_path() /
           ("picotracker-fourcc-xml-" + std::to_string(++sequence));
  }

  ~FourCCXmlFixture() {
    FileSystem::Install(previous_);
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  void Write(const char *name, const char *contents) {
    const std::filesystem::path path = root_ / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << contents;
  }

  void MakeDirectory(const char *name) {
    std::filesystem::create_directories(root_ / name);
  }

  [[nodiscard]] bool Exists(const char *name) const {
    return std::filesystem::exists(root_ / name);
  }

  [[nodiscard]] const std::filesystem::path &Root() const { return root_; }

  void RemoveAll(const char *name) {
    std::error_code error;
    std::filesystem::remove_all(root_ / name, error);
  }

  void WriteCommands(const char *name, FourCC *commands, std::size_t count) {
    auto file = filesystem_.Open((std::string("/") + name).c_str(), "w");
    REQUIRE(static_cast<bool>(file));
    tinyxml2::XMLPrinter printer(file.get());
    saveHexBuffer(&printer, "COMMAND1", commands,
                  static_cast<unsigned>(count));
    REQUIRE(file->Sync());
  }

  [[nodiscard]] std::string Read(const char *name) const {
    std::ifstream file(root_ / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

private:
  std::filesystem::path root_;
  WasmFileSystem filesystem_;
  FileSystem *previous_ = nullptr;
};

class FaultInjectFile final : public I_File {
public:
  FaultInjectFile(FileHandle delegate, bool failSync, bool failClose)
      : delegate_(std::move(delegate)), failSync_(failSync),
        failClose_(failClose) {}

  int Read(void *ptr, int size) override { return delegate_->Read(ptr, size); }
  int GetC() override { return delegate_->GetC(); }
  int Write(const void *ptr, int size, int count) override {
    return delegate_->Write(ptr, size, count);
  }
  void Seek(long offset, int whence) override {
    delegate_->Seek(offset, whence);
  }
  long Tell() override { return delegate_->Tell(); }
  int Error() override { return delegate_->Error(); }
  bool Sync() override {
    const bool synced = delegate_->Sync();
    return synced && !failSync_;
  }
  void Dispose() override { delete this; }

protected:
  bool Close() override {
    bool closed = true;
    if (delegate_) {
      I_File *raw =
          AcquireLegacyFileHandle_DO_NOT_USE(std::move(delegate_));
      closed = CloseFile_DO_NOT_USE(raw);
    }
    return closed && !failClose_;
  }

private:
  FileHandle delegate_;
  bool failSync_ = false;
  bool failClose_ = false;
};

class FaultInjectFileSystem final : public FileSystem {
public:
  explicit FaultInjectFileSystem(const std::filesystem::path &root)
      : delegate_(root.string()) {}

  void FailNextMoveTo(const char *path, std::uint8_t count = 1U) {
    failMoveDestination_ = path;
    failMoveCount_ = count;
  }
  void FailNextDelete(const char *path) {
    failDeletePath_ = path;
    failDeleteCount_ = 1U;
  }
  void FailNextSync(const char *path) {
    failSyncPath_ = path;
    failSyncCount_ = 1U;
  }
  void FailNextClose(const char *path) {
    failClosePath_ = path;
    failCloseCount_ = 1U;
  }
  void FailListOnCall(std::uint8_t call) { failListCountdown_ = call; }

  FileHandle Open(const char *name, const char *mode) override {
    FileHandle file = delegate_.Open(name, mode);
    if (!file)
      return file;
    const bool failSync =
        failSyncCount_ != 0U && failSyncPath_ == name;
    const bool failClose =
        failCloseCount_ != 0U && failClosePath_ == name;
    if (failSync)
      --failSyncCount_;
    if (failClose)
      --failCloseCount_;
    if (!failSync && !failClose)
      return file;
    return MakeFileHandle(
        new FaultInjectFile(std::move(file), failSync, failClose));
  }
  bool chdir(const char *path) override { return delegate_.chdir(path); }
  void list(etl::ivector<int> *indices, const char *filter, bool subDirOnly,
            bool includeHidden = false) override {
    delegate_.list(indices, filter, subDirOnly, includeHidden);
  }
  bool listChecked(etl::ivector<int> *indices, const char *filter,
                   bool subDirOnly, bool includeHidden = false) override {
    if (failListCountdown_ != 0U) {
      --failListCountdown_;
      if (failListCountdown_ == 0U) {
        indices->clear();
        return false;
      }
    }
    return delegate_.listChecked(indices, filter, subDirOnly, includeHidden);
  }
  void getFileName(int index, char *name, int length) override {
    delegate_.getFileName(index, name, length);
  }
  PicoFileType getFileType(int index) override {
    return delegate_.getFileType(index);
  }
  bool isParentRoot() override { return delegate_.isParentRoot(); }
  bool isCurrentRoot() override { return delegate_.isCurrentRoot(); }
  bool DeleteFile(const char *path) override {
    if (failDeleteCount_ != 0U && failDeletePath_ == path) {
      --failDeleteCount_;
      return false;
    }
    return delegate_.DeleteFile(path);
  }
  bool DeleteDir(const char *path) override {
    return delegate_.DeleteDir(path);
  }
  bool exists(const char *path) override { return delegate_.exists(path); }
  bool makeDir(const char *path, bool parents = false) override {
    return delegate_.makeDir(path, parents);
  }
  std::uint64_t getFileSize(int index) override {
    return delegate_.getFileSize(index);
  }
  bool CopyFile(const char *source, const char *target) override {
    return delegate_.CopyFile(source, target);
  }
  bool MoveFile(const char *source, const char *target) override {
    if (failMoveCount_ != 0U && failMoveDestination_ == target) {
      --failMoveCount_;
      return false;
    }
    return delegate_.MoveFile(source, target);
  }
  bool isExFat() override { return delegate_.isExFat(); }

private:
  WasmFileSystem delegate_;
  std::string failMoveDestination_;
  std::string failDeletePath_;
  std::string failSyncPath_;
  std::string failClosePath_;
  std::uint8_t failMoveCount_ = 0U;
  std::uint8_t failDeleteCount_ = 0U;
  std::uint8_t failSyncCount_ = 0U;
  std::uint8_t failCloseCount_ = 0U;
  std::uint8_t failListCountdown_ = 0U;
};

class TransactionByteState final : public Persistent {
public:
  TransactionByteState() : Persistent("TRANSACTION-STATE") {}

  unsigned char value = 0U;

private:
  void SaveContent(tinyxml2::XMLPrinter *printer) override {
    saveHexBuffer(printer, "VALUE", &value, 1U);
  }

  void RestoreContent(PersistencyDocument *document) override {
    if (!document->FirstChild() ||
        std::strcmp(document->ElemName(), "VALUE") != 0) {
      document->MarkError();
      return;
    }
    if (!restoreHexBuffer(document, &value, 1U))
      return;
  }
};

PersistencyService &TestPersistencyService() {
  // Service has no registry-unregister operation, so keep the one test service
  // alive for the entire test process.
  static PersistencyService service;
  return service;
}

class GenericRestoreInstrument final : public I_Instrument {
public:
  inline static constexpr const char *SidWaves[3] = {"--------", "TRI",
                                                      "SAW"};
  inline static constexpr const char *OpalAlgorithms[2] = {"1*2", "1+2"};

  explicit GenericRestoreInstrument(InstrumentType type)
      : I_Instrument(&variables_), type_(type),
        sidOsc_(FourCC::SIDInstrumentOSCNumber, 1),
        sidWave_(FourCC::SIDInstrumentWaveform, SidWaves, 3, 1),
        sidSync_(FourCC::SIDInstrumentVSync, false),
        opalAlgorithm_(FourCC::OPALInstrumentAlgorithm, OpalAlgorithms, 2, 0),
        opalFeedback_(FourCC::OPALInstrumentFeedback, 4),
        midiChannel_(FourCC::MidiInstrumentChannel, 7),
        table_(type == IT_MIDI ? FourCC::MidiInstrumentTable
                              : FourCC::SIDInstrumentTable,
               VAR_OFF) {
    variables_.push_back(&sidOsc_);
    variables_.push_back(&sidWave_);
    variables_.push_back(&sidSync_);
    variables_.push_back(&opalAlgorithm_);
    variables_.push_back(&opalFeedback_);
    variables_.push_back(&midiChannel_);
    variables_.push_back(&table_);
    SetName("OLD");
  }

  bool Init() override { return true; }
  bool Start(int, unsigned char, bool) override { return false; }
  void Stop(int) override {}
  void OnStart() override {}
  bool Render(int, fixed *, int, bool) override { return false; }
  bool IsInitialized() override { return true; }
  bool IsEmpty() override { return false; }
  InstrumentType GetType() override { return type_; }
  void ProcessCommand(int, FourCC, ushort) override {}
  int GetTable() override { return table_.GetInt(); }
  bool GetTableAutomation() override { return false; }
  void GetTableState(TableSaveState &) override {}
  void SetTableState(TableSaveState &) override {}
  etl::ilist<Variable *> *Variables() override { return &variables_; }

  int SidOsc() { return sidOsc_.GetInt(); }
  int SidWave() { return sidWave_.GetInt(); }
  int OpalAlgorithm() { return opalAlgorithm_.GetInt(); }
  int OpalFeedback() { return opalFeedback_.GetInt(); }
  int MidiChannel() { return midiChannel_.GetInt(); }
  void SetMidiChannel(int channel) { midiChannel_.SetInt(channel); }

private:
  etl::list<Variable *, 7> variables_;
  InstrumentType type_;
  Variable sidOsc_;
  Variable sidWave_;
  Variable sidSync_;
  Variable opalAlgorithm_;
  Variable opalFeedback_;
  Variable midiChannel_;
  Variable table_;
};

void LoadCommandElement(const char *file, FourCC *commands,
                        std::size_t count) {
  PersistencyDocument document;
  REQUIRE(document.Load(file));
  REQUIRE(document.FirstChild());
  CHECK(restoreHexBuffer(&document, commands, static_cast<unsigned>(count)));
  CHECK_FALSE(document.HadError());
}

bool TryLoadCommandElement(const char *file, FourCC *commands,
                           std::size_t count) {
  PersistencyDocument document;
  if (!document.Load(file) || !document.FirstChild())
    return false;
  return restoreHexBuffer(&document, commands, static_cast<unsigned>(count));
}

bool LoadByteElement(const char *file, unsigned char *bytes,
                     std::size_t capacity) {
  PersistencyDocument document;
  REQUIRE(document.Load(file));
  REQUIRE(document.FirstChild());
  const bool restored = restoreHexBuffer(&document, bytes, capacity);
  CHECK(document.HadError() != restored);
  return restored;
}

void AppendHexByte(std::string &xml, std::uint8_t byte) {
  static constexpr char digits[] = "0123456789ABCDEF";
  xml.push_back(digits[byte >> 4U]);
  xml.push_back(digits[byte & 0x0FU]);
}

struct ProjectRestoreTargets {
  static constexpr const char *ScaleChoices[2] = {"Chromatic", "Major"};
  static constexpr const char *RootChoices[2] = {"C", "C#"};
  Variable tempo{FourCC::VarTempo, 138};
  Variable preview{FourCC::VarPreviewVolume, 60};
  Variable scale{FourCC::VarScale, ScaleChoices, 2, 0};
  Variable root{FourCC::VarScaleRoot, RootChoices, 2, 0};
  Variable wrap{FourCC::VarWrap, false};
};

Variable *ResolveProjectRestoreTarget(void *context, const char *name) {
  auto &targets = *static_cast<ProjectRestoreTargets *>(context);
  if (std::strcmp(name, targets.tempo.GetName()) == 0)
    return &targets.tempo;
  if (std::strcmp(name, targets.preview.GetName()) == 0)
    return &targets.preview;
  if (std::strcmp(name, targets.scale.GetName()) == 0)
    return &targets.scale;
  if (std::strcmp(name, targets.root.GetName()) == 0)
    return &targets.root;
  if (std::strcmp(name, targets.wrap.GetName()) == 0)
    return &targets.wrap;
  return nullptr;
}

bool StageProjectParameters(const char *file, ProjectRestoreTargets &targets,
                            ProjectParameterRestorePacket &packet,
                            bool *hadError = nullptr) {
  PersistencyDocument document;
  if (!document.Load(file) || !document.FirstChild() ||
      std::strcmp(document.ElemName(), "PROJECT") != 0) {
    return false;
  }
  while (document.NextAttribute()) {
  }
  const bool restored = StageProjectParameterRestore(
      &document, &targets, ResolveProjectRestoreTarget, packet);
  if (hadError != nullptr)
    *hadError = document.HadError();
  return restored;
}

std::string EncodeCommandXml(const char *element,
                             std::span<const FourCC> commands,
                             std::size_t stride,
                             std::size_t encodedBytes) {
  std::string xml = std::string("<") + element + ">";
  const std::size_t fullSize = commands.size() * stride;
  encodedBytes = std::min(encodedBytes, fullSize);
  for (std::size_t offset = 0; offset < encodedBytes; offset += 64U) {
    xml += "<DATA>";
    const std::size_t end = std::min(offset + 64U, encodedBytes);
    for (std::size_t byte = offset; byte < end; ++byte) {
      const std::size_t command = byte / stride;
      const std::size_t wordByte = byte % stride;
      AppendHexByte(xml, wordByte == 0U
                             ? static_cast<std::uint8_t>(
                                   commands[command].get_value())
                             : 0U);
    }
    xml += "</DATA>";
  }
  xml += std::string("</") + element + ">";
  return xml;
}

} // namespace

TEST_CASE("Song COMMAND restore decodes canonical packed FX values") {
  FourCCXmlFixture fixture;
  fixture.Write("song.xml",
                "<COMMAND1><DATA>1E193A</DATA></COMMAND1>");
  std::array<FourCC, 3> commands{};
  LoadCommandElement("/song.xml", commands.data(), commands.size());

  CHECK(commands[0] == FourCC::InstrumentCommandKill);
  CHECK(commands[1] == FourCC::InstrumentCommandFilterResonance);
  CHECK(commands[2] == FourCC::InstrumentCommandTable);
  CHECK(std::string(commands[0].c_str()) == "KIL");
  CHECK(std::string(getHelpLegend(commands[0])[0]) == "KILl: --bb");
}

TEST_CASE("FourCC save writes canonical packed bytes without platform stride") {
  std::array<FourCC, 3> source{
      FourCC::InstrumentCommandKill,
      FourCC::InstrumentCommandFilterResonance,
      FourCC::InstrumentCommandTable};
  FourCCXmlFixture fixture;
  fixture.WriteCommands("roundtrip.xml", source.data(), source.size());
  const std::string xml = fixture.Read("roundtrip.xml");
  CHECK(xml.find("1E193A") != std::string::npos);
  CHECK(xml.find("1E000000") == std::string::npos);

  std::array<FourCC, 3> restored{};
  LoadCommandElement("/roundtrip.xml", restored.data(), restored.size());
  CHECK(restored == source);
}

TEST_CASE("Table CMD restore migrates legacy Web LE32 FX values") {
  FourCCXmlFixture fixture;
  fixture.Write("table.xml",
                "<CMD1><DATA>1E000000190000003A000000</DATA></CMD1>");
  std::array<FourCC, 3> commands{};
  LoadCommandElement("/table.xml", commands.data(), commands.size());

  CHECK(commands[0] == FourCC::InstrumentCommandKill);
  CHECK(commands[1] == FourCC::InstrumentCommandFilterResonance);
  CHECK(commands[2] == FourCC::InstrumentCommandTable);
  CHECK(std::string(commands[2].c_str()) == "TBL");
  CHECK(std::string(getHelpLegend(commands[2])[1]) == "run table bb");
}

TEST_CASE("legacy LE16 command buffers remain readable") {
  FourCCXmlFixture fixture;
  fixture.Write("le16.xml",
                "<COMMAND1><DATA>1E0019003A00</DATA></COMMAND1>");
  std::array<FourCC, 3> commands{};
  LoadCommandElement("/le16.xml", commands.data(), commands.size());

  CHECK(commands[0] == FourCC::InstrumentCommandKill);
  CHECK(commands[1] == FourCC::InstrumentCommandFilterResonance);
  CHECK(commands[2] == FourCC::InstrumentCommandTable);
}

TEST_CASE("maximum Song command arrays restore canonical and legacy LE32") {
  constexpr std::size_t commandCount = PHRASE_COUNT * STEPS_PER_PHRASE;
  std::array<FourCC, commandCount> source{};
  constexpr FourCC::enum_type pattern[] = {
      FourCC::InstrumentCommandArpeggiator,
      FourCC::InstrumentCommandKill,
      FourCC::InstrumentCommandFilterResonance,
      FourCC::InstrumentCommandTable,
      FourCC::InstrumentCommandNone};
  for (std::size_t index = 0; index < source.size(); ++index)
    source[index] = pattern[index % std::size(pattern)];

  FourCCXmlFixture fixture;
  fixture.WriteCommands("canonical-max.xml", source.data(), source.size());
  std::array<FourCC, commandCount> canonical{};
  LoadCommandElement("/canonical-max.xml", canonical.data(), canonical.size());
  CHECK(canonical == source);

  const std::string legacy =
      EncodeCommandXml("COMMAND1", source, 4U, source.size() * 4U);
  fixture.Write("le32-max.xml", legacy.c_str());
  std::array<FourCC, commandCount> restored{};
  LoadCommandElement("/le32-max.xml", restored.data(), restored.size());
  CHECK(restored == source);
}

TEST_CASE("truncated and invalid legacy commands become empty commands") {
  FourCCXmlFixture fixture;
  // The third LE32 word is truncated after its low 16 bits.
  fixture.Write(
      "truncated.xml",
      "<COMMAND1><DATA>1E000000190000003A00</DATA></COMMAND1>");
  std::array<FourCC, 3> truncated{};
  LoadCommandElement("/truncated.xml", truncated.data(), truncated.size());
  CHECK(truncated[0] == FourCC::InstrumentCommandKill);
  CHECK(truncated[1] == FourCC::InstrumentCommandFilterResonance);
  CHECK(truncated[2] == FourCC::InstrumentCommandNone);

  // A non-zero LE32 high byte and an unknown command ID are independently
  // rejected, while the following valid word is still recovered.
  fixture.Write(
      "invalid.xml",
      "<COMMAND1><DATA>1E000100FE0000003A000000</DATA></COMMAND1>");
  std::array<FourCC, 3> invalid{};
  LoadCommandElement("/invalid.xml", invalid.data(), invalid.size());
  CHECK(invalid[0] == FourCC::InstrumentCommandNone);
  CHECK(invalid[1] == FourCC::InstrumentCommandNone);
  CHECK(invalid[2] == FourCC::InstrumentCommandTable);

  fixture.Write("canonical-truncated.xml",
                "<COMMAND1><DATA>1EFE</DATA></COMMAND1>");
  std::array<FourCC, 4> canonical{};
  LoadCommandElement("/canonical-truncated.xml", canonical.data(),
                     canonical.size());
  CHECK(canonical[0] == FourCC::InstrumentCommandKill);
  CHECK(canonical[1] == FourCC::InstrumentCommandNone);
  CHECK(canonical[2] == FourCC::InstrumentCommandNone);
  CHECK(canonical[3] == FourCC::InstrumentCommandNone);
}

TEST_CASE("FourCC XML RLE length is capped to the destination migration bound") {
  FourCCXmlFixture fixture;
  fixture.Write(
      "bounded.xml",
      "<COMMAND2><DATA VALUE=\"30\" LENGTH=\"2147483647\"/></COMMAND2>");
  std::array<FourCC, 2> commands{};
  LoadCommandElement("/bounded.xml", commands.data(), commands.size());

  // The bounded prefix is interpreted as an overlong LE32 block. Its non-zero
  // high bytes make both words invalid instead of overflowing the destination.
  CHECK(commands[0] == FourCC::InstrumentCommandNone);
  CHECK(commands[1] == FourCC::InstrumentCommandNone);
}

TEST_CASE("FourCC command restore rejects malformed RLE and hex payloads") {
  FourCCXmlFixture fixture;
  const std::array<std::pair<const char *, const char *>, 7> cases{{
      {"missing-value.xml", "<COMMAND1><DATA LENGTH=\"1\"/></COMMAND1>"},
      {"missing-length.xml", "<COMMAND1><DATA VALUE=\"30\"/></COMMAND1>"},
      {"duplicate-value.xml",
       "<COMMAND1><DATA VALUE=\"30\" VALUE=\"31\" LENGTH=\"1\"/>"
       "</COMMAND1>"},
      {"duplicate-length.xml",
       "<COMMAND1><DATA VALUE=\"30\" LENGTH=\"1\" LENGTH=\"2\"/>"
       "</COMMAND1>"},
      {"zero-length.xml",
       "<COMMAND1><DATA VALUE=\"30\" LENGTH=\"0\"/></COMMAND1>"},
      {"odd-command-hex.xml", "<COMMAND1><DATA>1E0</DATA></COMMAND1>"},
      {"nonhex-command.xml", "<COMMAND1><DATA>1G</DATA></COMMAND1>"},
  }};

  for (const auto &[name, xml] : cases) {
    CAPTURE(name);
    fixture.Write(name, xml);
    std::array<FourCC, 2> commands{FourCC::InstrumentCommandKill,
                                  FourCC::InstrumentCommandKill};
    CHECK_FALSE(TryLoadCommandElement((std::string("/") + name).c_str(),
                                      commands.data(), commands.size()));
  }
}

TEST_CASE("byte buffer restore respects its explicit destination capacity") {
  FourCCXmlFixture fixture;
  fixture.Write(
      "bytes.xml",
      "<PARAM><DATA>0102</DATA><DATA VALUE=\"3\" LENGTH=\"2\"/></PARAM>");
  std::array<unsigned char, 6> bytes{0xAA, 0xAA, 0xAA,
                                     0xAA, 0xA5, 0x5A};
  CHECK(LoadByteElement("/bytes.xml", bytes.data(), 4U));
  const std::array<unsigned char, 6> expected{1, 2, 3, 3, 0xA5, 0x5A};
  CHECK(bytes == expected);
}

TEST_CASE("byte buffer restore rejects invalid and oversized RLE lengths") {
  FourCCXmlFixture fixture;
  std::array<unsigned char, 6> bytes{0, 0, 0, 0, 0xA5, 0x5A};

  fixture.Write("negative.xml",
                "<PARAM><DATA VALUE=\"1\" LENGTH=\"-1\"/></PARAM>");
  CHECK_FALSE(LoadByteElement("/negative.xml", bytes.data(), 4U));
  CHECK(bytes[4] == 0xA5);
  CHECK(bytes[5] == 0x5A);

  fixture.Write("oversized.xml",
                "<PARAM><DATA VALUE=\"1\" LENGTH=\"5\"/></PARAM>");
  CHECK_FALSE(LoadByteElement("/oversized.xml", bytes.data(), 4U));
  CHECK(bytes[4] == 0xA5);
  CHECK(bytes[5] == 0x5A);

  fixture.Write("invalid-number.xml",
                "<PARAM><DATA VALUE=\"1\" LENGTH=\"2x\"/></PARAM>");
  CHECK_FALSE(LoadByteElement("/invalid-number.xml", bytes.data(), 4U));

  fixture.Write("cumulative-overflow.xml",
                "<PARAM><DATA VALUE=\"1\" LENGTH=\"3\"/>"
                "<DATA VALUE=\"2\" LENGTH=\"2\"/></PARAM>");
  CHECK_FALSE(
      LoadByteElement("/cumulative-overflow.xml", bytes.data(), 4U));

  fixture.Write("invalid-value.xml",
                "<PARAM><DATA VALUE=\"256\" LENGTH=\"1\"/></PARAM>");
  CHECK_FALSE(LoadByteElement("/invalid-value.xml", bytes.data(), 4U));
}

TEST_CASE("byte buffer restore rejects odd and non-hex text") {
  FourCCXmlFixture fixture;
  std::array<unsigned char, 4> bytes{};

  fixture.Write("odd.xml", "<PARAM><DATA>001</DATA></PARAM>");
  CHECK_FALSE(LoadByteElement("/odd.xml", bytes.data(), bytes.size()));

  fixture.Write("nonhex.xml", "<PARAM><DATA>00G1</DATA></PARAM>");
  CHECK_FALSE(LoadByteElement("/nonhex.xml", bytes.data(), bytes.size()));

  fixture.Write("text-overflow.xml",
                "<PARAM><DATA>0001020304</DATA></PARAM>");
  CHECK_FALSE(
      LoadByteElement("/text-overflow.xml", bytes.data(), bytes.size()));
}

TEST_CASE("Project PARAMETER restore stages a complete payload before commit") {
  FourCCXmlFixture fixture;
  fixture.Write("project.xml",
                "<PROJECT VERSION=\"2.3\">"
                "<PARAMETER NAME=\"tempo\" VALUE=\"180\"/>"
                "<PARAMETER NAME=\"preview\" VALUE=\"70\"/>"
                "</PROJECT>");
  ProjectRestoreTargets targets;
  ProjectParameterRestorePacket packet{};
  bool hadError = true;

  REQUIRE(StageProjectParameters("/project.xml", targets, packet, &hadError));
  CHECK_FALSE(hadError);
  REQUIRE(packet.count == 2U);
  CHECK(targets.tempo.GetInt() == 138);
  CHECK(targets.preview.GetInt() == 60);

  for (std::uint8_t index = 0U; index < packet.count; ++index)
    packet.updates[index].target->SetString(packet.updates[index].value.data());
  CHECK(targets.tempo.GetInt() == 180);
  CHECK(targets.preview.GetInt() == 70);
}

TEST_CASE("Project version restore accepts release suffixes without float UB") {
  int version = 0;
  CHECK(ParseProjectVersionHundredthsForRestore("2.3-Beta3", version));
  CHECK(version == 230);
  CHECK(ParseProjectVersionHundredthsForRestore("2.30", version));
  CHECK(version == 230);
  CHECK(ParseProjectVersionHundredthsForRestore("2", version));
  CHECK(version == 200);

  CHECK_FALSE(ParseProjectVersionHundredthsForRestore("2e3", version));
  CHECK_FALSE(ParseProjectVersionHundredthsForRestore("2.", version));
  CHECK_FALSE(ParseProjectVersionHundredthsForRestore("2.3-", version));
  CHECK_FALSE(ParseProjectVersionHundredthsForRestore(
      "999999999999999999999999999999", version));
  CHECK_FALSE(ParseProjectVersionHundredthsForRestore("nan", version));

  int ratio = 0;
  CHECK(ParsePersistedIntegerAttribute("1", 1, 64, ratio));
  CHECK(ParsePersistedIntegerAttribute("64", 1, 64, ratio));
  CHECK_FALSE(ParsePersistedIntegerAttribute("0", 1, 64, ratio));
  CHECK_FALSE(ParsePersistedIntegerAttribute("65", 1, 64, ratio));
  CHECK_FALSE(ParsePersistedIntegerAttribute("1e1", 1, 64, ratio));
}

TEST_CASE("Project PARAMETER restore rejects malformed attributes atomically") {
  FourCCXmlFixture fixture;
  const std::string longAttribute(MAX_VARIABLE_STRING_LENGTH + 1U, 'X');
  const std::string parserOverflow(64U, 'Y');
  const std::array<std::pair<const char *, std::string>, 7> cases{{
      {"missing-name.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"180\"/>"
       "<PARAMETER VALUE=\"200\"/></PROJECT>"},
      {"missing-value.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\"/></PROJECT>"},
      {"long-name.xml", "<PROJECT><PARAMETER NAME=\"" + longAttribute +
                            "\" VALUE=\"180\"/></PROJECT>"},
      {"long-value.xml", "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"" +
                             longAttribute + "\"/></PROJECT>"},
      {"attribute-overflow.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"" + parserOverflow +
           "\"/></PROJECT>"},
      {"unterminated-attribute.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"180"},
      {"unterminated-project.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"180\"/>"},
  }};

  for (const auto &[name, xml] : cases) {
    CAPTURE(name);
    fixture.Write(name, xml.c_str());
    ProjectRestoreTargets targets;
    ProjectParameterRestorePacket packet{};
    bool hadError = false;
    CHECK_FALSE(StageProjectParameters((std::string("/") + name).c_str(),
                                       targets, packet, &hadError));
    CHECK(hadError);
    CHECK(packet.count == 0U);
    CHECK(targets.tempo.GetInt() == 138);
    CHECK(targets.preview.GetInt() == 60);
  }
}

TEST_CASE("Project PARAMETER semantic validation rejects unsafe values") {
  FourCCXmlFixture fixture;
  const std::array<std::pair<const char *, const char *>, 7> cases{{
      {"tempo-exponent.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"4e2\"/></PROJECT>"},
      {"tempo-overflow.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"401\"/></PROJECT>"},
      {"preview-overflow.xml",
       "<PROJECT><PARAMETER NAME=\"preview\" VALUE=\"100\"/></PROJECT>"},
      {"scale-invalid.xml",
       "<PROJECT><PARAMETER NAME=\"scale\" VALUE=\"INVALID\"/></PROJECT>"},
      {"root-invalid.xml",
       "<PROJECT><PARAMETER NAME=\"scaleroot\" VALUE=\"H\"/></PROJECT>"},
      {"bool-invalid.xml",
       "<PROJECT><PARAMETER NAME=\"wrap\" VALUE=\"1\"/></PROJECT>"},
      {"duplicate-target.xml",
       "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"180\"/>"
       "<PARAMETER NAME=\"tempo\" VALUE=\"181\"/></PROJECT>"},
  }};

  for (const auto &[name, xml] : cases) {
    CAPTURE(name);
    fixture.Write(name, xml);
    ProjectRestoreTargets targets;
    ProjectParameterRestorePacket packet{};
    REQUIRE(StageProjectParameters((std::string("/") + name).c_str(),
                                   targets, packet));
    CHECK_FALSE(ValidateProjectParameterRestorePacket(packet, 60, 400));
    CHECK(targets.tempo.GetInt() == 138);
    CHECK(targets.preview.GetInt() == 60);
    CHECK(targets.scale.GetInt() == 0);
    CHECK(targets.root.GetInt() == 0);
    CHECK_FALSE(targets.wrap.GetBool());
  }
}

TEST_CASE("Project PARAMETER semantic validation preserves canonical choices") {
  FourCCXmlFixture fixture;
  fixture.Write("project-semantic-valid.xml",
                "<PROJECT><PARAMETER NAME=\"tempo\" VALUE=\"400\"/>"
                "<PARAMETER NAME=\"preview\" VALUE=\"99\"/>"
                "<PARAMETER NAME=\"scale\" VALUE=\"Major\"/>"
                "<PARAMETER NAME=\"scaleroot\" VALUE=\"C#\"/>"
                "<PARAMETER NAME=\"wrap\" VALUE=\"true\"/></PROJECT>");
  ProjectRestoreTargets targets;
  ProjectParameterRestorePacket packet{};
  REQUIRE(StageProjectParameters("/project-semantic-valid.xml", targets,
                                 packet));
  CHECK(ValidateProjectParameterRestorePacket(packet, 60, 400));
}

TEST_CASE("Song and Groove semantic restore rejects unsafe indexes and rolls "
          "back") {
  FourCCXmlFixture fixture;
  fixture.MakeDirectory("projects");
  PersistencyService &service = TestPersistencyService();
  static Song song;
  TableHolder *tables = TableHolder::GetInstance();
  Groove *groove = Groove::GetInstance();

  song.Reset();
  tables->Reset();
  groove->Clear();
  song.chain_.data_[0] = 0x01U;
  song.phrase_.instr_[0] = 0x00U;
  REQUIRE(service.SaveLoadRollback() == PERSIST_SAVED);

  const auto rejectSong = [&](const char *project, const char *field,
                              unsigned value) {
    CAPTURE(project);
    CAPTURE(field);
    const std::string xml =
        std::string("<PICOTRACKER><SONG><") + field +
        "><DATA VALUE=\"" + std::to_string(value) +
        "\" LENGTH=\"1\"/></" + field + "></SONG></PICOTRACKER>";
    fixture.Write((std::string("projects/") + project + "/lgptsav.dat").c_str(),
                  xml.c_str());
    CHECK(service.Load(project) == PERSIST_LOAD_FAILED);
    REQUIRE(service.RestoreLoadRollback() == PERSIST_LOADED);
    CHECK(song.chain_.data_[0] == 0x01U);
    CHECK(song.phrase_.instr_[0] == 0x00U);
  };

  rejectSong("CHAIN80", "CHAINS", 0x80U);
  rejectSong("CHAINFE", "CHAINS", 0xFEU);
  rejectSong("BADINST", "INSTRUMENTS", MAX_INSTRUMENT_COUNT);

  fixture.Write("projects/SONGFE/lgptsav.dat",
                "<PICOTRACKER><SONG><SONG><DATA VALUE=\"254\" "
                "LENGTH=\"1\"/></SONG></SONG></PICOTRACKER>");
  CHECK(service.Load("SONGFE") == PERSIST_LOADED);
  CHECK(song.data_[0] == 0xFEU);
  CHECK(song.chain_.IsUsed(0xFEU));
  REQUIRE(service.RestoreLoadRollback() == PERSIST_LOADED);

  fixture.Write("projects/BADGROOVE/lgptsav.dat",
                "<PICOTRACKER><GROOVES><DATA><DATA>00</DATA></DATA>"
                "</GROOVES></PICOTRACKER>");
  CHECK(service.Load("BADGROOVE") == PERSIST_LOAD_FAILED);
  CHECK(groove->GetGrooveData(0)[0] == 6U);
  groove->SetGroove(0, 0);
  CHECK_NOTHROW(static_cast<void>(groove->TriggerChannel(0)));
  REQUIRE(service.RestoreLoadRollback() == PERSIST_LOADED);
  CHECK(groove->GetGrooveData(0)[0] == 6U);
  service.ClearLoadRollback();
}

TEST_CASE("serialized load rollback recovers state after semantic failure") {
  FourCCXmlFixture fixture;
  fixture.Write("projects/.keep", "");
  fixture.Write("projects/CURRENT/autosave.dat", "DO NOT TOUCH");

  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  state.value = 0x2AU;
  REQUIRE(service.SaveLoadRollback() == PERSIST_SAVED);

  fixture.Write("projects/BAD/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA VALUE=\"7\" LENGTH=\"2\"/>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  state.value = 0U;
  CHECK(service.Load("BAD") == PERSIST_LOAD_FAILED);
  CHECK(service.RestoreLoadRollback() == PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
  CHECK(fixture.Read("projects/CURRENT/autosave.dat") == "DO NOT TOUCH");
  service.ClearLoadRollback();
}

TEST_CASE("project persistence rejects unsafe and reserved user names") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();

  CHECK(PersistencyService::IsValidProjectName("DEMO"));
  CHECK(PersistencyService::IsValidProjectName(".hidden"));
  CHECK(PersistencyService::IsInternalProjectName(UNNAMED_PROJECT_NAME));
  CHECK(PersistencyService::IsInternalProjectName(
      STAGING_BACKUP_PROJECT_NAME));
  CHECK(PersistencyService::IsInternalProjectName(
      ".picotracker-saveas-stage.DEMO"));
  CHECK(PersistencyService::IsInternalProjectName(
      ".picotracker-saveas-backup.DEMO"));
  CHECK_FALSE(
      PersistencyService::IsInternalProjectName(".saveas-stage.X"));
  CHECK_FALSE(PersistencyService::IsValidProjectName(nullptr));
  CHECK_FALSE(PersistencyService::IsValidProjectName(""));
  CHECK_FALSE(PersistencyService::IsValidProjectName("."));
  CHECK_FALSE(PersistencyService::IsValidProjectName(".."));
  CHECK_FALSE(PersistencyService::IsValidProjectName("A/B"));
  CHECK_FALSE(PersistencyService::IsValidProjectName("A\\B"));
  CHECK_FALSE(PersistencyService::IsValidProjectName(UNNAMED_PROJECT_NAME));
  CHECK(PersistencyService::IsValidProjectName(".saveas-stage.X"));
  CHECK_FALSE(
      PersistencyService::IsValidProjectName("12345678901234567"));

  CHECK(service.Save(UNNAMED_PROJECT_NAME, "SOURCE", true) == PERSIST_ERROR);
  CHECK(service.SaveProjectState(UNNAMED_PROJECT_NAME) == PERSIST_ERROR);
  CHECK(service.AutoSaveProjectData(UNNAMED_PROJECT_NAME) == PERSIST_ERROR);
  CHECK_FALSE(service.ClearAutosave(UNNAMED_PROJECT_NAME));
  CHECK(service.Validate(UNNAMED_PROJECT_NAME) == PERSIST_LOAD_FAILED);
  CHECK(service.Load(UNNAMED_PROJECT_NAME) == PERSIST_LOAD_FAILED);
  CHECK(service.Save("../ESCAPE", "SOURCE", true) == PERSIST_ERROR);
  CHECK(service.Load("../ESCAPE") == PERSIST_LOAD_FAILED);
  CHECK_FALSE(service.Exists("../ESCAPE"));
  CHECK_FALSE(service.DeleteProject("../ESCAPE"));
  CHECK_FALSE(fixture.Exists("ESCAPE"));
}

TEST_CASE("failed Save As sample copy removes the incomplete target") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.MakeDirectory("projects/SOURCE/samples/broken.wav");

  CHECK(service.Save("TARGET", "SOURCE", true) == PERSIST_ERROR);
  CHECK_FALSE(fixture.Exists("projects/TARGET"));
  CHECK(fixture.Exists("projects/SOURCE/samples/broken.wav"));
}

TEST_CASE("Save As directory scan failure preserves the existing target") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/SOURCE/samples/kick.wav", "new-kick");
  fixture.Write("projects/TARGET/samples/kick.wav", "old-kick");
  fixture.Write("projects/TARGET/lgptsav.dat", "<PICOTRACKER/>");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  // First checked scan recovers stale transaction directories; the second is
  // the source sample listing that must not fail open as an empty directory.
  failingFileSystem.FailListOnCall(2U);

  CHECK(service.Save("TARGET", "SOURCE", true) == PERSIST_ERROR);
  CHECK(fixture.Read("projects/TARGET/samples/kick.wav") == "old-kick");
  CHECK(fixture.Read("projects/TARGET/lgptsav.dat") == "<PICOTRACKER/>");
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-stage.TARGET"));
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-backup.TARGET"));
}

TEST_CASE("Save As may copy from the internal untitled staging project") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.untitled/samples/kick.wav", "sample-bytes");

  CHECK(service.Save("RENAMED", UNNAMED_PROJECT_NAME, true) == PERSIST_SAVED);
  CHECK(fixture.Read("projects/RENAMED/samples/kick.wav") == "sample-bytes");
  CHECK(fixture.Exists("projects/RENAMED/lgptsav.dat"));
}

TEST_CASE("Save As overwrite atomically replaces the complete sample set") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/SOURCE/samples/kick.wav", "new-kick");
  fixture.Write("projects/TARGET/samples/kick.wav", "old-kick");
  fixture.Write("projects/TARGET/samples/stale.wav", "must-disappear");
  fixture.Write("projects/TARGET/lgptsav.dat", "<PICOTRACKER/>");

  CHECK(service.Save("TARGET", "SOURCE", true) == PERSIST_SAVED);
  CHECK(fixture.Read("projects/TARGET/samples/kick.wav") == "new-kick");
  CHECK_FALSE(fixture.Exists("projects/TARGET/samples/stale.wav"));
  CHECK(fixture.Exists("projects/TARGET/lgptsav.dat"));
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-stage.TARGET"));
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-backup.TARGET"));
}

TEST_CASE("Save As install failure restores the original target") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/SOURCE/samples/kick.wav", "new-kick");
  fixture.Write("projects/TARGET/samples/kick.wav", "old-kick");
  fixture.Write("projects/TARGET/lgptsav.dat", "<PICOTRACKER/>");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextMoveTo("/projects/TARGET");

  CHECK(service.Save("TARGET", "SOURCE", true) == PERSIST_ERROR);
  CHECK(fixture.Read("projects/TARGET/samples/kick.wav") == "old-kick");
  CHECK(fixture.Read("projects/TARGET/lgptsav.dat") == "<PICOTRACKER/>");
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-stage.TARGET"));
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-backup.TARGET"));
}

TEST_CASE("Save As boot recovery rolls back an uninstalled stage") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.picotracker-saveas-stage.TARGET/samples/kick.wav",
                "new");
  fixture.Write("projects/.picotracker-saveas-stage.TARGET/lgptsav.dat",
                "<PICOTRACKER/>");
  fixture.Write("projects/.picotracker-saveas-backup.TARGET/samples/kick.wav",
                "old");
  fixture.Write("projects/.picotracker-saveas-backup.TARGET/lgptsav.dat",
                "<PICOTRACKER/>");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOAD_FAILED);
  CHECK(fixture.Read("projects/TARGET/samples/kick.wav") == "old");
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-stage.TARGET"));
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-backup.TARGET"));
}

TEST_CASE("Save As boot recovery prefers backup over a corrupt new target") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/TARGET/lgptsav.dat", "<PICOTRACKER>");
  fixture.Write("projects/TARGET/samples/kick.wav", "corrupt-new");
  fixture.Write("projects/.picotracker-saveas-backup.TARGET/lgptsav.dat",
                "<PICOTRACKER/>");
  fixture.Write("projects/.picotracker-saveas-backup.TARGET/samples/kick.wav",
                "old");
  fixture.Write(".current", "TARGET");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "TARGET");
  CHECK(fixture.Read("projects/TARGET/samples/kick.wav") == "old");
  CHECK_FALSE(
      fixture.Exists("projects/.picotracker-saveas-backup.TARGET"));
}

TEST_CASE("internal untitled save path preserves the public name boundary") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  REQUIRE(service.CreateProject() == PERSIST_SAVED);

  CHECK(service.Save(UNNAMED_PROJECT_NAME, "", false) == PERSIST_ERROR);
  CHECK(PersistencyServiceTestPeer::SaveStaging(service) == PERSIST_SAVED);
  CHECK(PersistencyServiceTestPeer::SaveStagingState(service) ==
        PERSIST_SAVED);
  CHECK(fixture.Exists("projects/.untitled/lgptsav.dat"));
  CHECK(fixture.Read(".current") == UNNAMED_PROJECT_NAME);
}

TEST_CASE("untitled boot recovery rolls back a complete pre-commit replacement") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-session");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-session");
  // Simulate power loss after the new directory is complete, but before
  // Session commits its durable replacement phase.
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "old-session");
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("named project is restored when untitled phase loses power with old staging") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-staging");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-candidate");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  CHECK(fixture.Read(".current") == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Read(".current.bak") == "A");

  // Power loss here: state marker installed, phase COMMITTED not written.
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "A");
  CHECK(fixture.Read(".current") == "A");
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "old-staging");
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
}

TEST_CASE("named project is restored when empty untitled phase loses power") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  bool hadPrevious = true;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  REQUIRE_FALSE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-candidate");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  CHECK(fixture.Read(".current") == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Read(".current.bak") == "A");

  // PENDING:0 means the parseable candidate is still uncommitted and must be
  // removed, while the previous named project pointer is restored.
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "A");
  CHECK(fixture.Read(".current") == "A");
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
}

TEST_CASE("empty-stage rollback keeps pending until candidate deletion succeeds") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  bool hadPrevious = true;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  REQUIRE_FALSE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "candidate");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextDelete("identity.wav");

  CHECK_FALSE(PersistencyServiceTestPeer::RollbackStagingReplacement(
      service, hadPrevious));
  CHECK(fixture.Exists("projects/.untitled"));
  CHECK(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));

  // A reboot/retry consumes the same durable PENDING:0 record, removes the
  // uncommitted candidate and restores A instead of loading `.untitled`.
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "A");
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
}

TEST_CASE("first untitled marker failure rolls back the only uncommitted candidate") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  bool hadPrevious = true;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, ""));
  REQUIRE_FALSE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "candidate");

  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextSync("/.current.tmp");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "") ==
          PERSIST_ERROR);
  REQUIRE(PersistencyServiceTestPeer::RollbackStagingReplacement(
      service, hadPrevious));

  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists(".current"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("committed untitled survives pending marker cleanup failure") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-staging");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-committed");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextDelete(STAGING_TRANSACTION_PENDING_FILE);

  REQUIRE(PersistencyServiceTestPeer::CommitStagingReplacement(
      service, hadPrevious));
  CHECK(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "new-committed");
  REQUIRE(PersistencyServiceTestPeer::LoadStaging(service) == PERSIST_LOADED);
  REQUIRE(PersistencyServiceTestPeer::FinalizeCommittedStagingReplacement(
      service));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("untitled boot recovery keeps a committed replacement before cleanup") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-session");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-session");
  // Force cleanup to be deferred after the durable COMMITTED phase.
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextDelete(
      "identity.wav");
  REQUIRE(PersistencyServiceTestPeer::CommitStagingReplacement(
      service, hadPrevious));
  REQUIRE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "new-session");
  REQUIRE(PersistencyServiceTestPeer::LoadStaging(service) == PERSIST_LOADED);
  REQUIRE(PersistencyServiceTestPeer::FinalizeCommittedStagingReplacement(
      service));
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("corrupt committed untitled restores named pointer before directory rollback") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-staging");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER>");
  fixture.Write("projects/.untitled/samples/identity.wav", "corrupt-new");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  // Simulate power loss after the durable phase marker was installed but
  // before the previous untitled directory could be cleaned up.
  fixture.Write(&STAGING_TRANSACTION_COMMIT_FILE[1], "COMMITTED");

  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  // The first recovery attempt loses power/fails while rewriting `.current`.
  // Directory rollback must not run before this marker is durable.
  failingFileSystem.FailNextSync("/.current.tmp");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOAD_FAILED);
  CHECK(fixture.Exists("projects/.untitled.session-backup"));
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "corrupt-new");

  // A reboot retries from PENDING+COMMITTED, restores A first, then rolls the
  // old staging directory back. It must never mistake that valid old staging
  // payload for the committed new project.
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "A");
  CHECK(fixture.Read(".current") == "A");
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "old-staging");
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("corrupt committed empty-stage untitled returns to named project") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  bool hadPrevious = true;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  REQUIRE_FALSE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER>");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  fixture.Write(&STAGING_TRANSACTION_COMMIT_FILE[1], "COMMITTED");

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "A");
  CHECK(fixture.Read(".current") == "A");
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("corrupt first-boot committed untitled returns to fresh-create state") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  bool hadPrevious = true;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, ""));
  REQUIRE_FALSE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER>");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "") ==
          PERSIST_SAVED);
  fixture.Write(&STAGING_TRANSACTION_COMMIT_FILE[1], "COMMITTED");

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOAD_FAILED);
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists(".current"));
  CHECK_FALSE(fixture.Exists(".current.tmp"));
  CHECK_FALSE(fixture.Exists(".current.bak"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("semantic-invalid committed untitled keeps rollback until Session rejects it") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-staging");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA VALUE=\"7\" LENGTH=\"2\"/>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  fixture.Write(&STAGING_TRANSACTION_COMMIT_FILE[1], "COMMITTED");

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Exists("projects/.untitled.session-backup"));
  CHECK(fixture.Exists(".current.bak"));
  CHECK(PersistencyServiceTestPeer::LoadStaging(service) ==
        PERSIST_LOAD_FAILED);

  char previous[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(PersistencyServiceTestPeer::RollbackCommittedStagingReplacement(
      service, previous));
  CHECK(std::string(previous) == "A");
  CHECK(fixture.Read(".current") == "A");
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "old-staging");
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("semantic-good recovered untitled finalizes backups only after load") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "A");
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-staging");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious, "A"));
  fixture.Write("projects/.untitled/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>2A</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  REQUIRE(PersistencyServiceTestPeer::SaveStagingTransactionState(service,
                                                                  "A") ==
          PERSIST_SAVED);
  fixture.Write(&STAGING_TRANSACTION_COMMIT_FILE[1], "COMMITTED");

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(fixture.Exists("projects/.untitled.session-backup"));
  CHECK(fixture.Exists(".current.bak"));
  state.value = 0U;
  REQUIRE(PersistencyServiceTestPeer::LoadStaging(service) == PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
  REQUIRE(PersistencyServiceTestPeer::FinalizeCommittedStagingReplacement(
      service));
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(".current.bak"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
}

TEST_CASE("forced untitled purge removes an interrupted replacement completely") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-session");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-session");
  fixture.Write("projects/A/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/B/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current.tmp", "B");
  fixture.Write(".current.bak", "A");
  fixture.Write(".current.bak.tmp", "A");

  REQUIRE(service.PurgeUnnamedProject());
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PURGE_FILE[1]));
  CHECK_FALSE(fixture.Exists(".current"));
  CHECK_FALSE(fixture.Exists(".current.tmp"));
  CHECK_FALSE(fixture.Exists(".current.bak"));
  CHECK_FALSE(fixture.Exists(".current.bak.tmp"));

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOAD_FAILED);
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
}

TEST_CASE("interrupted forced untitled purge resumes instead of restoring backup") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "old-session");
  bool hadPrevious = false;
  REQUIRE(PersistencyServiceTestPeer::BeginStagingReplacement(
      service, hadPrevious));
  REQUIRE(hadPrevious);
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "new-session");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextDelete("identity.wav");

  CHECK_FALSE(service.PurgeUnnamedProject());
  REQUIRE(fixture.Exists(&STAGING_TRANSACTION_PURGE_FILE[1]));
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOAD_FAILED);
  CHECK_FALSE(fixture.Exists("projects/.untitled"));
  CHECK_FALSE(fixture.Exists("projects/.untitled.session-backup"));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PENDING_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_COMMIT_FILE[1]));
  CHECK_FALSE(fixture.Exists(&STAGING_TRANSACTION_PURGE_FILE[1]));
}

TEST_CASE("invalid current marker reaches missing untitled first-boot create") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write(".current", "MISSING");
  fixture.MakeDirectory("projects");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  const bool stagingPayloadExists =
      fixture.Exists("projects/.untitled/lgptsav.dat") ||
      fixture.Exists("projects/.untitled/autosave.dat");
  CHECK_FALSE(stagingPayloadExists);
  CHECK_FALSE(tracker_session_detail::ShouldPreflightProjectLoad(
      false, true, stagingPayloadExists));
  CHECK(service.CreateProject() == PERSIST_SAVED);
  CHECK(fixture.Exists("projects/.untitled/lgptsav.dat"));
}

TEST_CASE("missing current marker preserves an existing valid untitled project") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/.untitled/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/.untitled/samples/identity.wav", "keep-session");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Read("projects/.untitled/samples/identity.wav") ==
        "keep-session");
  CHECK(tracker_session_detail::ShouldPreflightProjectLoad(false, true, true));
}

TEST_CASE("invalid current marker recovers untitled base journal before boot") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write(".current", "MISSING");
  fixture.Write("projects/.untitled/lgptsav.bak", "<PICOTRACKER/>");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == UNNAMED_PROJECT_NAME);
  CHECK(fixture.Exists("projects/.untitled/lgptsav.dat"));
  CHECK_FALSE(fixture.Exists("projects/.untitled/lgptsav.bak"));
  CHECK(tracker_session_detail::ShouldPreflightProjectLoad(false, true, true));
}

TEST_CASE("legacy project resembling old transaction prefix survives boot") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  constexpr const char *legacyName = ".saveas-stage.X";
  static_assert(std::char_traits<char>::length(legacyName) <=
                MAX_PROJECT_NAME_LENGTH);
  fixture.Write("projects/.saveas-stage.X/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", legacyName);
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == legacyName);
  CHECK(fixture.Exists("projects/.saveas-stage.X/lgptsav.dat"));
}

TEST_CASE("semantic autosave failure requires reset before explicit base load") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.MakeDirectory("projects/PROJECT/samples");
  state.value = 0x2AU;
  REQUIRE(service.Save("PROJECT", "", false) == PERSIST_SAVED);
  fixture.Write("projects/PROJECT/autosave.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA VALUE=\"7\" LENGTH=\"2\"/>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  state.value = 0U;

  CHECK(service.Load("PROJECT") == PERSIST_LOAD_FAILED);
  // TrackerApplicationSession performs a complete Project/SamplePool/Table
  // reset at this boundary before using the private base-only load.
  state.value = 0U;
  CHECK(PersistencyServiceTestPeer::LoadBase(service, "PROJECT") ==
        PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
}

TEST_CASE("semantic-invalid base destination retains and can promote backup") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.Write("projects/PROJECT/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA VALUE=\"7\" LENGTH=\"2\"/>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  fixture.Write("projects/PROJECT/lgptsav.bak",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>2A</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");

  // Structural preflight accepts the destination but must not finalize the
  // only previous generation before semantic restore.
  REQUIRE(service.Validate("PROJECT") == PERSIST_LOADED);
  CHECK(fixture.Exists("projects/PROJECT/lgptsav.bak"));
  state.value = 0U;
  CHECK(service.Load("PROJECT") == PERSIST_LOAD_FAILED);
  CHECK(fixture.Exists("projects/PROJECT/lgptsav.bak"));

  // Session performs a full model reset at this boundary, then loads and
  // promotes the semantic-good backup transactionally.
  state.value = 0U;
  REQUIRE(PersistencyServiceTestPeer::LoadJournalBackup(
              service, "PROJECT", false) == PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
  REQUIRE(PersistencyServiceTestPeer::PromoteJournalBackup(
      service, "PROJECT", false));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/lgptsav.bak"));
  state.value = 0U;
  REQUIRE(service.Load("PROJECT") == PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
}

TEST_CASE("semantic autosave backup remains available until explicit finalize") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.Write("projects/PROJECT/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>11</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  fixture.Write("projects/PROJECT/autosave.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>33</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  fixture.Write("projects/PROJECT/autosave.bak",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>22</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");

  state.value = 0U;
  REQUIRE(service.Load("PROJECT") == PERSIST_LOADED);
  CHECK(state.value == 0x33U);
  CHECK(fixture.Exists("projects/PROJECT/autosave.bak"));
  REQUIRE(PersistencyServiceTestPeer::FinalizeJournal(service, "PROJECT",
                                                       true));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/autosave.bak"));
}

TEST_CASE("autosave backup journal is recovered before loading") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.MakeDirectory("projects/PROJECT/samples");
  state.value = 0x55U;
  REQUIRE(service.AutoSaveProjectData("PROJECT") == PERSIST_SAVED);
  std::filesystem::rename(fixture.Root() / "projects/PROJECT/autosave.dat",
                          fixture.Root() / "projects/PROJECT/autosave.bak");
  state.value = 0U;

  CHECK(service.Load("PROJECT") == PERSIST_LOADED);
  CHECK(state.value == 0x55U);
  CHECK(fixture.Exists("projects/PROJECT/autosave.dat"));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/autosave.bak"));
}

TEST_CASE("autosave journal is synced and leaves no visible siblings") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.MakeDirectory("projects/PROJECT/samples");
  state.value = 0x33U;

  CHECK(service.AutoSaveProjectData("PROJECT") == PERSIST_SAVED);
  CHECK(fixture.Exists("projects/PROJECT/autosave.dat"));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/autosave.tmp"));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/autosave.bak"));
  state.value = 0U;
  CHECK(service.Load("PROJECT") == PERSIST_LOADED);
  CHECK(state.value == 0x33U);
}

TEST_CASE("explicit save reports an autosave deletion failure") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/PROJECT/autosave.dat", "<PICOTRACKER/>");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextDelete("/projects/PROJECT/autosave.dat");

  CHECK(service.Save("PROJECT", "", false) == PERSIST_ERROR);
  CHECK(fixture.Exists("projects/PROJECT/lgptsav.dat"));
  CHECK(fixture.Exists("projects/PROJECT/autosave.dat"));
}

TEST_CASE("autosave clear keeps destination when stale backup cleanup fails") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.Write("projects/PROJECT/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/PROJECT/autosave.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>2A</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  fixture.Write("projects/PROJECT/autosave.bak",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>55</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextDelete("/projects/PROJECT/autosave.bak");

  CHECK_FALSE(service.ClearAutosave("PROJECT"));
  CHECK(fixture.Exists("projects/PROJECT/autosave.dat"));
  CHECK(fixture.Exists("projects/PROJECT/autosave.bak"));

  // Reboot recovery treats the still-present destination as authoritative,
  // removes the stale backup and never resurrects its older value.
  state.value = 0U;
  REQUIRE(service.Load("PROJECT") == PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
  REQUIRE(PersistencyServiceTestPeer::FinalizeJournal(service, "PROJECT",
                                                       true));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/autosave.bak"));
}

TEST_CASE("normal save install failure restores the previous base") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/PROJECT/lgptsav.dat", "<PICOTRACKER/>");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  // Fail POSIX replace and then the FAT-style temp install; rollback is the
  // third move to this destination and is allowed through.
  failingFileSystem.FailNextMoveTo("/projects/PROJECT/lgptsav.dat", 2U);

  CHECK(service.Save("PROJECT", "", false) == PERSIST_ERROR);
  CHECK(fixture.Read("projects/PROJECT/lgptsav.dat") == "<PICOTRACKER/>");
  CHECK_FALSE(fixture.Exists("projects/PROJECT/lgptsav.tmp"));
  CHECK_FALSE(fixture.Exists("projects/PROJECT/lgptsav.bak"));
}

TEST_CASE("normal save sync failure preserves the previous base") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/PROJECT/lgptsav.dat", "<PICOTRACKER/>");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextSync("/projects/PROJECT/lgptsav.tmp");

  CHECK(service.Save("PROJECT", "", false) == PERSIST_ERROR);
  CHECK(fixture.Read("projects/PROJECT/lgptsav.dat") == "<PICOTRACKER/>");
  CHECK_FALSE(fixture.Exists("projects/PROJECT/lgptsav.tmp"));
}

TEST_CASE("project state replacement is synced and preserves old state on "
          "temp failure") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write(".current", "OLD");
  fixture.MakeDirectory(".current.tmp");

  CHECK(service.SaveProjectState("NEW") == PERSIST_ERROR);
  CHECK(fixture.Read(".current") == "OLD");

  fixture.RemoveAll(".current.tmp");
  CHECK(service.SaveProjectState("NEW") == PERSIST_SAVED);
  CHECK(fixture.Read(".current") == "NEW");
  CHECK_FALSE(fixture.Exists(".current.tmp"));
  CHECK_FALSE(fixture.Exists(".current.bak"));
}

TEST_CASE("project state close failure preserves the previous marker") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write(".current", "OLD");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextClose("/.current.tmp");

  CHECK(service.SaveProjectState("NEW") == PERSIST_ERROR);
  CHECK(fixture.Read(".current") == "OLD");
  CHECK_FALSE(fixture.Exists(".current.tmp"));
}

TEST_CASE("project state loader recovers the FAT replacement journal") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/OLD/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current.bak", "OLD");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "OLD");
  CHECK(fixture.Read(".current") == "OLD");
  CHECK_FALSE(fixture.Exists(".current.bak"));
}

TEST_CASE("project state loader prefers synced temp and retains FAT backup") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/OLD/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/NEW/lgptsav.dat", "<PICOTRACKER/>");
  // Power loss after current(old)->backup and before temp(new)->current.
  fixture.Write(".current.bak", "OLD");
  fixture.Write(".current.tmp", "NEW");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "NEW");
  CHECK(fixture.Read(".current") == "NEW");
  CHECK(fixture.Read(".current.bak") == "OLD");
  CHECK_FALSE(fixture.Exists(".current.tmp"));
}

TEST_CASE("project state loader falls back when temp install itself fails") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/OLD/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/NEW/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current.bak", "OLD");
  fixture.Write(".current.tmp", "NEW");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  failingFileSystem.FailNextMoveTo("/.current");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "OLD");
  CHECK(fixture.Read(".current") == "OLD");
  CHECK_FALSE(fixture.Exists(".current.tmp"));
}

TEST_CASE("project state loader promotes a synced first-save temp") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/FIRST/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current.tmp", "FIRST");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "FIRST");
  CHECK(fixture.Read(".current") == "FIRST");
  CHECK_FALSE(fixture.Exists(".current.tmp"));
}

TEST_CASE("project state validation recovers a pointer to a valid backup") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/BAD/lgptsav.dat", "<PICOTRACKER>");
  fixture.Write("projects/GOOD/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "BAD");
  fixture.Write(".current.bak", "GOOD");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "GOOD");
  CHECK(fixture.Read(".current") == "GOOD");
  CHECK_FALSE(fixture.Exists(".current.bak"));
}

TEST_CASE("semantic current failure retains and exposes previous marker") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  TransactionByteState state;
  fixture.Write("projects/BAD/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA VALUE=\"7\" LENGTH=\"2\"/>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  fixture.Write("projects/GOOD/lgptsav.dat",
                "<PICOTRACKER><TRANSACTION-STATE><VALUE>"
                "<DATA>2A</DATA>"
                "</VALUE></TRANSACTION-STATE></PICOTRACKER>");
  fixture.Write(".current", "BAD");
  fixture.Write(".current.bak", "GOOD");
  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};

  REQUIRE(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "BAD");
  CHECK(fixture.Exists(".current.bak"));
  CHECK(service.Load("BAD") == PERSIST_LOAD_FAILED);
  char previous[MAX_PROJECT_NAME_LENGTH + 1U]{};
  REQUIRE(PersistencyServiceTestPeer::ReadPreviousProject(service, previous));
  CHECK(std::string(previous) == "GOOD");
  state.value = 0U; // Session's full reset boundary.
  CHECK(service.Load("GOOD") == PERSIST_LOADED);
  CHECK(state.value == 0x2AU);
  CHECK(service.SaveProjectState("GOOD") == PERSIST_SAVED);
  CHECK_FALSE(fixture.Exists(".current.bak"));
}

TEST_CASE("semantic fallback never deletes its only good marker before promote") {
  FourCCXmlFixture fixture;
  PersistencyService &service = TestPersistencyService();
  fixture.Write("projects/BAD/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write("projects/GOOD/lgptsav.dat", "<PICOTRACKER/>");
  fixture.Write(".current", "BAD");
  fixture.Write(".current.bak", "GOOD");
  FaultInjectFileSystem failingFileSystem(fixture.Root());
  FileSystem::Install(&failingFileSystem);
  // First failure forces the FAT path. The second simulates power/I/O loss
  // after BAD was removed but before the known-good backup was promoted.
  failingFileSystem.FailNextMoveTo("/.current", 2U);

  CHECK(service.SaveProjectState("GOOD") == PERSIST_ERROR);
  CHECK_FALSE(fixture.Exists(".current"));
  CHECK(fixture.Read(".current.bak") == "GOOD");
  CHECK(fixture.Read(".current.tmp") == "GOOD");

  char project[MAX_PROJECT_NAME_LENGTH + 1U]{};
  CHECK(service.LoadCurrentProjectName(project) == PERSIST_LOADED);
  CHECK(std::string(project) == "GOOD");
  CHECK(fixture.Read(".current") == "GOOD");
}

TEST_CASE("Table restore rejects missing malformed and out-of-range IDs") {
  FourCCXmlFixture fixture;
  TestPersistencyService();

  const auto checkRejected = [&](const char *name, const char *xml) {
    fixture.Write(name, xml);
    PersistencyDocument document;
    REQUIRE(document.Load((std::string("/") + name).c_str()));
    REQUIRE(document.FirstChild());
    REQUIRE(std::strcmp(document.ElemName(), "TABLES") == 0);
    TableHolder holder;
    holder.RestoreContent(&document);
    CHECK(document.HadError());
  };

  checkRejected("table-missing-id.xml",
                "<TABLES><TABLE><CMD1/></TABLE></TABLES>");
  checkRejected("table-nonhex-id.xml",
                "<TABLES><TABLE ID=\"G0\"><CMD1/></TABLE></TABLES>");
  checkRejected("table-large-id.xml",
                "<TABLES><TABLE ID=\"20\"><CMD1/></TABLE></TABLES>");
}

TEST_CASE("Instrument payload validation accepts a complete legacy subset") {
  FourCCXmlFixture fixture;
  fixture.Write("complete.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"channel\" "
                "VALUE=\"7\"/></INSTRUMENT>");
  CHECK(ValidateInstrumentFilePayload("/complete.pti"));
}

TEST_CASE("Instrument payload validation accepts only OFF or allocated table "
          "IDs") {
  FourCCXmlFixture fixture;
  fixture.Write("table-off.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"table\" "
                "VALUE=\"-1\"/></INSTRUMENT>");
  fixture.Write("table-last.pti",
                "<INSTRUMENT TYPE=\"SAMPLE\"><PARAM NAME=\"TABLE\" "
                "VALUE=\"31\"/></INSTRUMENT>");
  fixture.Write("table-overflow.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"table\" "
                "VALUE=\"32\"/></INSTRUMENT>");
  fixture.Write("table-seven-bit.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"table\" "
                "VALUE=\"127\"/></INSTRUMENT>");
  fixture.Write("table-negative.pti",
                "<INSTRUMENT TYPE=\"SAMPLE\"><PARAM NAME=\"table\" "
                "VALUE=\"-2\"/></INSTRUMENT>");
  fixture.Write("table-nonnumeric.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"table\" "
                "VALUE=\"1F\"/></INSTRUMENT>");

  CHECK(ValidateInstrumentFilePayload("/table-off.pti"));
  CHECK(ValidateInstrumentFilePayload("/table-last.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/table-overflow.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/table-seven-bit.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/table-negative.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/table-nonnumeric.pti"));

  CHECK(IsValidTableIndex(0));
  CHECK(IsValidTableIndex(TABLE_COUNT - 1));
  CHECK_FALSE(IsValidTableIndex(TABLE_COUNT));
  CHECK(IsValidInstrumentTableValue(-1));
  CHECK_FALSE(IsValidInstrumentTableValue(0x7F));
}

TEST_CASE("Instrument payload validation preserves sample filenames") {
  FourCCXmlFixture fixture;
  fixture.Write("sample-name.pti",
                "<INSTRUMENT TYPE=\"SAMPLE\"><PARAM NAME=\"sample\" "
                "VALUE=\"kick.wav\"/></INSTRUMENT>");
  fixture.Write("sample-mixed-case.pti",
                "<INSTRUMENT TYPE=\"SAMPLE\"><PARAM NAME=\"sample\" "
                "VALUE=\"AKWF_0001.WAV\"/></INSTRUMENT>");

  CHECK(ValidateInstrumentFilePayload("/sample-name.pti"));
  CHECK(ValidateInstrumentFilePayload("/sample-mixed-case.pti"));
}

TEST_CASE("Sample instrument persisted integer ranges match audio contracts") {
  struct Boundary {
    FourCC id;
    int minimum;
    int maximum;
  };
  const std::array<Boundary, 12> boundaries{{
      {FourCC::SampleInstrumentVolume, 0, 255},
      {FourCC::SampleInstrumentPan, 0, 254},
      {FourCC::SampleInstrumentRootNote, 0, 127},
      {FourCC::SampleInstrumentFineTune, 0, 255},
      {FourCC::SampleInstrumentCrushVolume, 0, 255},
      {FourCC::SampleInstrumentCrush, 1, 16},
      {FourCC::SampleInstrumentDownsample, 0, 8},
      {FourCC::SampleInstrumentFilterCutOff, 0, 255},
      {FourCC::SampleInstrumentFilterResonance, 0, 255},
      {FourCC::SampleInstrumentFilterType, 0, 255},
      {FourCC::SampleInstrumentStart, 0, 0x0FFFFFFF},
      {FourCC::SampleInstrumentEnd, 0, 0x0FFFFFFF},
  }};

  for (const Boundary &boundary : boundaries) {
    CAPTURE(boundary.id.get_value());
    int minimum = 0;
    int maximum = 0;
    REQUIRE(SampleInstrumentParameterLimits::TryGetPersistedIntegerRange(
        boundary.id, minimum, maximum));
    CHECK(minimum == boundary.minimum);
    CHECK(maximum == boundary.maximum);

    int parsed = 0;
    CHECK(ParsePersistedIntegerAttribute(std::to_string(minimum).c_str(),
                                         minimum, maximum, parsed));
    CHECK(ParsePersistedIntegerAttribute(std::to_string(maximum).c_str(),
                                         minimum, maximum, parsed));
    CHECK_FALSE(ParsePersistedIntegerAttribute(
        std::to_string(static_cast<long long>(minimum) - 1LL).c_str(), minimum,
        maximum, parsed));
    CHECK_FALSE(ParsePersistedIntegerAttribute(
        std::to_string(static_cast<long long>(maximum) + 1LL).c_str(), minimum,
        maximum, parsed));
    CHECK_FALSE(ParsePersistedIntegerAttribute("1e2", minimum, maximum,
                                                parsed));
  }
}

TEST_CASE("Instrument payload validation rejects truncated and empty files") {
  FourCCXmlFixture fixture;
  fixture.Write("truncated.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"channel\" "
                "VALUE=\"7\"/>");
  fixture.Write("empty.pti", "<INSTRUMENT TYPE=\"MIDI\"></INSTRUMENT>");
  fixture.Write("missing-value.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"channel\"/>"
                "</INSTRUMENT>");
  fixture.Write("missing-name.pti",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM VALUE=\"7\"/>"
                "</INSTRUMENT>");
  const std::string longAttribute(MAX_VARIABLE_STRING_LENGTH + 1U, 'X');
  const std::string longInstrumentName(MAX_INSTRUMENT_NAME_LENGTH + 1U, 'N');
  fixture.Write("long-name.pti",
                ("<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"" + longAttribute +
                 "\" VALUE=\"7\"/></INSTRUMENT>")
                    .c_str());
  fixture.Write("long-value.pti",
                ("<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"channel\" "
                 "VALUE=\"" +
                 longAttribute + "\"/></INSTRUMENT>")
                    .c_str());
  fixture.Write("long-instrument-name.pti",
                ("<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"InstrumentName\" "
                 "VALUE=\"" +
                 longInstrumentName + "\"/></INSTRUMENT>")
                    .c_str());
  fixture.Write("missing-type.pti",
                "<INSTRUMENT><PARAM NAME=\"channel\" VALUE=\"7\"/>"
                "</INSTRUMENT>");
  fixture.Write("unknown-type.pti",
                "<INSTRUMENT TYPE=\"UNKNOWN\"><PARAM NAME=\"channel\" "
                "VALUE=\"7\"/></INSTRUMENT>");
  fixture.Write("duplicate-type.pti",
                "<INSTRUMENT TYPE=\"MIDI\" TYPE=\"MIDI\">"
                "<PARAM NAME=\"channel\" VALUE=\"7\"/></INSTRUMENT>");
  fixture.Write("standalone-id.pti",
                "<INSTRUMENT ID=\"00\" TYPE=\"MIDI\">"
                "<PARAM NAME=\"channel\" VALUE=\"7\"/></INSTRUMENT>");

  CHECK_FALSE(ValidateInstrumentFilePayload("/truncated.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/empty.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/missing-value.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/missing-name.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/long-name.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/long-value.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/long-instrument-name.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/missing-type.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/unknown-type.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/duplicate-type.pti"));
  CHECK_FALSE(ValidateInstrumentFilePayload("/standalone-id.pti"));
}

TEST_CASE("Instrument type detection bounds the envelope and preserves legacy versions") {
  FourCCXmlFixture fixture;
  fixture.MakeDirectory("instruments");
  PersistencyService &service = TestPersistencyService();
  const std::string legacyVersion(60U, 'V');
  fixture.Write(
      "instruments/legacy.pti",
      ("<INSTRUMENT VERSION=\"" + legacyVersion +
       "\" TYPE=\"midi\"><PARAM NAME=\"channel\" VALUE=\"7\"/>"
       "</INSTRUMENT>")
          .c_str());
  fixture.Write("instruments/duplicate.pti",
                "<INSTRUMENT TYPE=\"MIDI\" TYPE=\"SAMPLE\">"
                "<PARAM NAME=\"channel\" VALUE=\"7\"/></INSTRUMENT>");
  fixture.Write("instruments/missing.pti",
                "<INSTRUMENT><PARAM NAME=\"channel\" VALUE=\"7\"/>"
                "</INSTRUMENT>");

  CHECK(service.DetectInstrumentType("legacy.pti") == IT_MIDI);
  CHECK(service.DetectInstrumentType("duplicate.pti") == IT_NONE);
  CHECK(service.DetectInstrumentType("missing.pti") == IT_NONE);
  CHECK(service.DetectInstrumentType("../legacy.pti") == IT_NONE);
  CHECK(service.DetectInstrumentType("legacy.pti.tmp") == IT_NONE);
}

TEST_CASE("Instrument journal recovery restores the last committed export") {
  FourCCXmlFixture fixture;
  fixture.MakeDirectory("instruments");
  fixture.Write("instruments/lead.pti",
                "<INSTRUMENT TYPE=\"UNKNOWN\"><PARAM NAME=\"channel\" "
                "VALUE=\"15\"/></INSTRUMENT>");
  fixture.Write("instruments/lead.bak",
                "<INSTRUMENT TYPE=\"MIDI\"><PARAM NAME=\"channel\" "
                "VALUE=\"7\"/></INSTRUMENT>");
  fixture.Write("instruments/lead.tmp", "<INSTRUMENT");

  PersistencyService &service = TestPersistencyService();
  REQUIRE(service.RecoverInstrumentExports());
  CHECK(fixture.Exists("instruments/lead.pti"));
  CHECK_FALSE(fixture.Exists("instruments/lead.bak"));
  CHECK_FALSE(fixture.Exists("instruments/lead.tmp"));
  CHECK(service.DetectInstrumentType("lead.pti") == IT_MIDI);
}

TEST_CASE("Instrument service exports a validated pti and preserves overwrite contract") {
  FourCCXmlFixture fixture;
  fixture.MakeDirectory("instruments");
  PersistencyService &service = TestPersistencyService();
  GenericRestoreInstrument source(IT_MIDI);
  source.SetMidiChannel(15);

  CHECK(service.ExportInstrument(&source,
                                 etl::string<MAX_INSTRUMENT_NAME_LENGTH>(
                                     "lead"),
                                 false) == PERSIST_SAVED);
  CHECK(fixture.Exists("instruments/lead.pti"));
  CHECK(service.DetectInstrumentType("lead.pti") == IT_MIDI);
  CHECK(service.ExportInstrument(&source,
                                 etl::string<MAX_INSTRUMENT_NAME_LENGTH>(
                                     "lead"),
                                 false) == PERSIST_EXISTS);

  GenericRestoreInstrument restored(IT_MIDI);
  REQUIRE(service.ImportInstrument(&restored, "lead.pti") == PERSIST_LOADED);
  CHECK(restored.MidiChannel() == 15);
}

TEST_CASE("Generic SID pti restore rejects unsafe values without partial mutation") {
  FourCCXmlFixture fixture;
  fixture.MakeDirectory("instruments");
  fixture.Write("instruments/unsafe-osc.pti",
                "<INSTRUMENT TYPE=\"SID\">"
                "<PARAM NAME=\"OSCNUM\" VALUE=\"15\"/>"
                "</INSTRUMENT>");
  fixture.Write("instruments/unsafe.pti",
                "<INSTRUMENT TYPE=\"SID\">"
                "<PARAM NAME=\"OSCNUM\" VALUE=\"2\"/>"
                "<PARAM NAME=\"VWF\" VALUE=\"UNKNOWN\"/>"
                "</INSTRUMENT>");
  fixture.Write("instruments/noncanonical-bool.pti",
                "<INSTRUMENT TYPE=\"SID\">"
                "<PARAM NAME=\"VSYNC\" VALUE=\"FALSE\"/>"
                "</INSTRUMENT>");
  GenericRestoreInstrument instrument(IT_SID);
  PersistencyService &service = TestPersistencyService();
  REQUIRE(instrument.FindVariable("VSYNC") != nullptr);
  CHECK(instrument.FindVariable("VSYNC")->GetType() == Variable::BOOL);

  CHECK(service.ImportInstrument(&instrument, "unsafe-osc.pti") ==
        PERSIST_ERROR);
  CHECK(instrument.SidOsc() == 1);
  CHECK(service.ImportInstrument(&instrument, "unsafe.pti") == PERSIST_ERROR);
  CHECK(instrument.SidOsc() == 1);
  CHECK(instrument.SidWave() == 1);
  CHECK(service.ImportInstrument(&instrument, "noncanonical-bool.pti") ==
        PERSIST_ERROR);
  CHECK(std::string(instrument.GetUserSetName().c_str()) == "OLD");
}

TEST_CASE("Generic OPAL project restore stages values and rejects truncation") {
  FourCCXmlFixture fixture;
  fixture.Write("unsafe-opal.xml",
                "<INSTRUMENT TYPE=\"OPAL\">"
                "<PARAM NAME=\"FEEDBACK\" VALUE=\"7\"/>"
                "<PARAM NAME=\"ALGORITHM\" VALUE=\"UNKNOWN\"/>"
                "</INSTRUMENT>");
  fixture.Write("truncated-opal.xml",
                "<INSTRUMENT TYPE=\"OPAL\">"
                "<PARAM NAME=\"FEEDBACK\" VALUE=\"7\"/>");
  GenericRestoreInstrument instrument(IT_OPAL);

  for (const char *path : {"/unsafe-opal.xml", "/truncated-opal.xml"}) {
    PersistencyDocument document;
    REQUIRE(document.Load(path));
    REQUIRE(document.FirstChild());
    REQUIRE(instrument.Restore(&document));
    CHECK(document.HadError());
    CHECK(instrument.OpalFeedback() == 4);
    CHECK(instrument.OpalAlgorithm() == 0);
  }
}

TEST_CASE("Generic MIDI restore enforces channel bounds and commits valid input") {
  FourCCXmlFixture fixture;
  fixture.MakeDirectory("instruments");
  fixture.Write("instruments/bad-midi.pti",
                "<INSTRUMENT TYPE=\"MIDI\">"
                "<PARAM NAME=\"channel\" VALUE=\"16\"/>"
                "</INSTRUMENT>");
  fixture.Write("instruments/good-midi.pti",
                "<INSTRUMENT TYPE=\"MIDI\">"
                "<PARAM NAME=\"channel\" VALUE=\"15\"/>"
                "</INSTRUMENT>");
  GenericRestoreInstrument instrument(IT_MIDI);
  PersistencyService &service = TestPersistencyService();

  CHECK(service.ImportInstrument(&instrument, "bad-midi.pti") == PERSIST_ERROR);
  CHECK(instrument.MidiChannel() == 7);
  CHECK(service.ImportInstrument(&instrument, "good-midi.pti") ==
        PERSIST_LOADED);
  CHECK(instrument.MidiChannel() == 15);
}

TEST_CASE("Compact instrument siblings preserve each first parameter") {
  FourCCXmlFixture fixture;
  fixture.Write("compact-bank.xml",
                "<INSTRUMENTBANK>"
                "<INSTRUMENT ID=\"00\" TYPE=\"MIDI\">"
                "<PARAM NAME=\"channel\" VALUE=\"15\"/>"
                "</INSTRUMENT>"
                "<INSTRUMENT ID=\"01\" TYPE=\"SID\">"
                "<PARAM NAME=\"OSCNUM\" VALUE=\"2\"/>"
                "</INSTRUMENT>"
                "</INSTRUMENTBANK>");
  PersistencyDocument document;
  REQUIRE(document.Load("/compact-bank.xml"));
  REQUIRE(document.FirstChild());
  REQUIRE(std::strcmp(document.ElemName(), "INSTRUMENTBANK") == 0);
  REQUIRE(document.FirstChild());

  const auto consumeEnvelope = [&]() {
    bool hasId = false;
    bool hasType = false;
    bool attribute = document.NextAttribute();
    while (attribute) {
      hasId = hasId || std::strcmp(document.attrname_, "ID") == 0;
      hasType = hasType || std::strcmp(document.attrname_, "TYPE") == 0;
      if (hasId && hasType)
        break;
      attribute = document.NextAttribute();
    }
    REQUIRE(hasId);
    REQUIRE(hasType);
  };

  consumeEnvelope();
  GenericRestoreInstrument midi(IT_MIDI);
  midi.RestoreContent(&document);
  REQUIRE_FALSE(document.HadError());
  CHECK(midi.MidiChannel() == 15);
  REQUIRE(document.NextSibling());

  consumeEnvelope();
  GenericRestoreInstrument sid(IT_SID);
  sid.RestoreContent(&document);
  REQUIRE_FALSE(document.HadError());
  CHECK(sid.SidOsc() == 2);
  CHECK_FALSE(document.NextSibling());
  CHECK(document.Finish());
}

TEST_CASE("Instrument bank restore policy rejects malformed and duplicate slots") {
  std::uint8_t slot = 0U;
  InstrumentType type = IT_NONE;
  CHECK(DecodeInstrumentBankSlotId("00", slot));
  CHECK(slot == 0U);
  CHECK(DecodeInstrumentBankSlotId("0f", slot));
  CHECK(slot == 15U);
  CHECK_FALSE(DecodeInstrumentBankSlotId("0", slot));
  CHECK_FALSE(DecodeInstrumentBankSlotId("000", slot));
  CHECK_FALSE(DecodeInstrumentBankSlotId("GG", slot));
  CHECK_FALSE(DecodeInstrumentBankSlotId("27", slot));

  CHECK(DecodeInstrumentBankType("sample", type));
  CHECK(type == IT_SAMPLE);
  CHECK(DecodeInstrumentBankType("SID", type));
  CHECK(type == IT_SID);
  CHECK_FALSE(DecodeInstrumentBankType("NONE", type));
  CHECK_FALSE(DecodeInstrumentBankType("UNKNOWN", type));

  InstrumentBankRestorePolicy policy;
  CHECK(policy.Reserve(0U, IT_SAMPLE));
  CHECK_FALSE(policy.Reserve(0U, IT_MIDI));
}

TEST_CASE("Instrument bank restore policy rejects fixed-pool exhaustion") {
  InstrumentBankRestorePolicy policy;
  for (std::uint8_t index = 0U; index < MAX_SIDINSTRUMENT_COUNT; ++index)
    CHECK(policy.Reserve(index, IT_SID));
  CHECK_FALSE(policy.Reserve(MAX_SIDINSTRUMENT_COUNT, IT_SID));

  InstrumentBankRestorePolicy opalPolicy;
  for (std::uint8_t index = 0U; index < MAX_OPALINSTRUMENT_COUNT; ++index)
    CHECK(opalPolicy.Reserve(index, IT_OPAL));
  CHECK_FALSE(opalPolicy.Reserve(MAX_OPALINSTRUMENT_COUNT, IT_OPAL));
}
