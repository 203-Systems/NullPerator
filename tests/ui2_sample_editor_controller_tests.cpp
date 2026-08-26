#include "doctest/doctest.h"

#include "Application/Model/Config.h"
#include "Application/UI2/Controllers/Ui2SampleEditorController.h"
#include "Application/UI2/Controllers/Ui2SampleSlicesController.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

class MemorySampleFile final : public I_File {
public:
  MemorySampleFile(const std::uint8_t *bytes, std::size_t size)
      : bytes_(bytes), size_(size) {}

  int Read(void *destination, int size) override {
    if (destination == nullptr || size <= 0)
      return 0;
    const std::size_t count = std::min<std::size_t>(
        static_cast<std::size_t>(size), size_ - position_);
    if (count > 0U) {
      std::memcpy(destination, bytes_ + position_, count);
      position_ += count;
    }
    if (count != static_cast<std::size_t>(size))
      error_ = true;
    return static_cast<int>(count);
  }
  int GetC() override {
    if (position_ >= size_)
      return -1;
    return bytes_[position_++];
  }
  int Write(const void *, int, int) override { return 0; }
  void Seek(long offset, int whence) override {
    std::int64_t base = 0;
    if (whence == SEEK_CUR)
      base = static_cast<std::int64_t>(position_);
    else if (whence == SEEK_END)
      base = static_cast<std::int64_t>(size_);
    const std::int64_t next = base + offset;
    if (next < 0 || next > static_cast<std::int64_t>(size_)) {
      error_ = true;
      position_ = next < 0 ? 0U : size_;
    } else {
      position_ = static_cast<std::size_t>(next);
    }
  }
  long Tell() override { return static_cast<long>(position_); }
  int Error() override { return error_ ? 1 : 0; }
  bool Sync() override { return true; }
  void Dispose() override { delete this; }

protected:
  bool Close() override { return true; }

private:
  const std::uint8_t *bytes_ = nullptr;
  std::size_t size_ = 0U;
  std::size_t position_ = 0U;
  bool error_ = false;
};

class SampleWaveFileSystem final : public FileSystem {
public:
  SampleWaveFileSystem() {
    previous_ = FileSystem::GetInstance();
    FileSystem::Install(this);
  }
  ~SampleWaveFileSystem() override { FileSystem::Install(previous_); }

  void BuildPcm(std::uint32_t frames = 512U, std::uint16_t channels = 1U) {
    size_ = 0U;
    AppendFourCc("RIFF");
    AppendU32(36U + frames * channels * 2U);
    AppendFourCc("WAVE");
    AppendFourCc("fmt ");
    AppendU32(16U);
    AppendU16(1U);
    AppendU16(channels);
    AppendU32(44100U);
    AppendU32(44100U * channels * 2U);
    AppendU16(static_cast<std::uint16_t>(channels * 2U));
    AppendU16(16U);
    AppendFourCc("data");
    AppendU32(frames * channels * 2U);
    for (std::uint32_t frame = 0U; frame < frames; ++frame) {
      const std::int32_t phase = static_cast<std::int32_t>(frame % 128U);
      const std::int16_t value = static_cast<std::int16_t>(
          (phase < 64 ? phase : 127 - phase) * 900 - 28000);
      for (std::uint16_t channel = 0U; channel < channels; ++channel)
        AppendU16(static_cast<std::uint16_t>(value));
    }
  }

  void BuildInvalid() {
    size_ = 12U;
    std::memcpy(bytes_.data(), "NOT A WAVE!", size_);
  }

  void SetOpenFailureAfter(std::uint8_t successfulOpens) {
    failAfter_ = successfulOpens;
  }

  FileHandle Open(const char *path, const char *) override {
    lastPath_.fill('\0');
    if (path != nullptr)
      std::snprintf(lastPath_.data(), lastPath_.size(), "%s", path);
    if (std::strcmp(lastPath_.data(), "MISSING.WAV") == 0 ||
        openCount_ >= failAfter_)
      return {};
    ++openCount_;
    return MakeFileHandle(new MemorySampleFile(bytes_.data(), size_));
  }

  bool chdir(const char *) override { return false; }
  void list(etl::ivector<int> *, const char *, bool, bool = false) override {}
  void getFileName(int, char *, int) override {}
  PicoFileType getFileType(int) override { return PFT_UNKNOWN; }
  bool isParentRoot() override { return false; }
  bool isCurrentRoot() override { return false; }
  bool DeleteFile(const char *) override { return false; }
  bool DeleteDir(const char *) override { return false; }
  bool exists(const char *) override { return false; }
  bool makeDir(const char *, bool = false) override { return false; }
  std::uint64_t getFileSize(int) override { return size_; }
  bool CopyFile(const char *, const char *) override { return false; }
  bool MoveFile(const char *, const char *) override { return false; }
  bool isExFat() override { return false; }

  const char *LastPath() const { return lastPath_.data(); }

private:
  void AppendFourCc(const char *value) {
    std::memcpy(bytes_.data() + size_, value, 4U);
    size_ += 4U;
  }
  void AppendU16(std::uint16_t value) {
    bytes_[size_++] = static_cast<std::uint8_t>(value);
    bytes_[size_++] = static_cast<std::uint8_t>(value >> 8U);
  }
  void AppendU32(std::uint32_t value) {
    for (std::uint8_t byte = 0U; byte < 4U; ++byte)
      bytes_[size_++] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }

  FileSystem *previous_ = nullptr;
  std::array<std::uint8_t, 8192> bytes_{};
  std::array<char, PFILENAME_SIZE> lastPath_{};
  std::size_t size_ = 0U;
  std::uint8_t openCount_ = 0U;
  std::uint8_t failAfter_ = 0xFFU;
};

template <typename Controller>
auto Tap(Controller &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

template <typename Controller>
auto Chord(Controller &controller, TrackerAction modifier,
           TrackerAction action) {
  controller.Handle(modifier, true);
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  controller.Handle(modifier, false);
  return command;
}

} // namespace

TEST_CASE("UI2 waveform backend streams real project and library WAV masks") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(512U, 2U);
  Ui2SampleWaveformBackend waveform;

  REQUIRE(waveform.LoadProjectPool(fileSystem, "DEMO", "KICK.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  CHECK(std::strcmp(waveform.Path(),
                    "/projects/DEMO/samples/KICK.WAV") == 0);
  CHECK(waveform.FrameCount() == 512U);
  CHECK(waveform.ChannelCount() == 2U);
  Ui2WaveformSnapshot editor;
  REQUIRE(waveform.BuildMask(editor,
                             Ui2SampleWaveformBackend::EditorMaskHeight) ==
          Ui2SampleWaveformBuildResult::Built);
  CHECK(editor.size > 0U);
  CHECK(editor.revision != 0U);

  REQUIRE(waveform.LoadLibrary(fileSystem, "DRUMS/SNARE.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  CHECK(std::strcmp(waveform.Path(), "/samples/DRUMS/SNARE.WAV") == 0);
  Ui2WaveformSnapshot slices;
  REQUIRE(waveform.BuildMask(slices,
                             Ui2SampleWaveformBackend::SlicesMaskHeight) ==
          Ui2SampleWaveformBuildResult::Built);
  CHECK(slices.size > 0U);
}

TEST_CASE("UI2 waveform backend rejects absent and malformed WAV safely") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm();
  Ui2SampleWaveformBackend waveform;
  CHECK(waveform.LoadPath(fileSystem, "MISSING.WAV") ==
        Ui2SampleWaveformLoadResult::OpenFailed);
  CHECK_FALSE(waveform.Ready());

  fileSystem.BuildInvalid();
  CHECK(waveform.LoadPath(fileSystem, "BAD.WAV") ==
        Ui2SampleWaveformLoadResult::InvalidWav);
  CHECK_FALSE(waveform.Ready());
}

TEST_CASE("UI2 project waveform backend rejects non-flat sample paths") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm();
  Ui2SampleWaveformBackend waveform;

  CHECK(waveform.LoadProjectPool(fileSystem, "DEMO", "NESTED/KICK.WAV") ==
        Ui2SampleWaveformLoadResult::InvalidPath);
  CHECK(waveform.LoadProjectPool(fileSystem, "DEMO", "NESTED\\KICK.WAV") ==
        Ui2SampleWaveformLoadResult::InvalidPath);
  CHECK(waveform.LoadProjectPool(fileSystem, "DEMO", "..") ==
        Ui2SampleWaveformLoadResult::InvalidPath);
  CHECK_FALSE(waveform.Ready());
  CHECK(fileSystem.LastPath()[0] == '\0');
}

TEST_CASE("UI2 waveform backend reports a mask reopen failure") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm();
  fileSystem.SetOpenFailureAfter(1U);
  Ui2SampleWaveformBackend waveform;
  REQUIRE(waveform.LoadPath(fileSystem, "ONE.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  Ui2WaveformSnapshot packet;
  CHECK(waveform.BuildMask(packet, 72U) ==
        Ui2SampleWaveformBuildResult::OpenFailed);
  CHECK(packet.size == 0U);
}

TEST_CASE("UI2 waveform zoom and pan clamp to sample bounds") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(2048U);
  Ui2SampleWaveformBackend waveform;
  REQUIRE(waveform.LoadPath(fileSystem, "LONG.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  CHECK(waveform.SetZoomLevel(0xFFU, 2047U));
  CHECK(waveform.ZoomLevel() == waveform.MaxZoomLevel());
  CHECK(waveform.ViewEnd() == waveform.FrameCount());
  const std::uint32_t rightStart = waveform.ViewStart();
  CHECK_FALSE(waveform.PanColumns(1000));
  CHECK(waveform.ViewStart() == rightStart);
  CHECK(waveform.CenterOn(0U));
  CHECK(waveform.ViewStart() == 0U);
  CHECK_FALSE(waveform.PanColumns(-1000));
}

TEST_CASE("UI2 Sample Editor exposes real endpoints markers zoom and preview") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(512U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleEditorController controller(waveform);
  REQUIRE(controller.OpenProjectPool(fileSystem, "DEMO", "Kick.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);

  SampleEditorViewUi2Snapshot snapshot = controller.Snapshot();
  CHECK(snapshot.waveformReady);
  CHECK(std::strcmp(snapshot.name.data(), "Kick") == 0);
  CHECK(std::strcmp(snapshot.start.data(), "0000000") == 0);
  CHECK(std::strcmp(snapshot.end.data(), "00001FF") == 0);
  REQUIRE(snapshot.markers.count == 2U);
  CHECK(snapshot.markers.markers[0].kind == Ui2WaveformMarkerKind::Start);
  CHECK(snapshot.markers.markers[1].kind == Ui2WaveformMarkerKind::End);

  const Ui2SampleEditorCommand preview =
      controller.Handle(TrackerAction::Play, true);
  CHECK(preview.type == Ui2SampleEditorCommandType::PreviewStart);
  CHECK(preview.start == 0U);
  CHECK(preview.end == 511U);
  CHECK(preview.singleCycle);
  CHECK(controller.Snapshot().playing);
  CHECK(controller.Handle(TrackerAction::Play, false).type ==
        Ui2SampleEditorCommandType::PreviewStop);

  controller.SetFocus(SampleEditorViewUi2Focus::Waveform);
  Chord(controller, TrackerAction::Option, TrackerAction::Right);
  const Ui2SampleEditorCommand moved =
      Chord(controller, TrackerAction::Edit, TrackerAction::Left);
  CHECK(moved.type == Ui2SampleEditorCommandType::SetEnd);
  CHECK(moved.value < 511U);
  CHECK(controller.End() == moved.value);

  CHECK(waveform.ZoomLevel() == 0U);
  Chord(controller, TrackerAction::Option, TrackerAction::Up);
  CHECK(waveform.ZoomLevel() == 1U);
  CHECK(controller.Snapshot().waveformReady);
}

TEST_CASE("UI2 single-cycle preview capacity counts interleaved channels") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(301U, 2U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleEditorController editor(waveform);
  REQUIRE(editor.OpenPath(fileSystem, "STEREO.WAV", false) ==
          Ui2SampleWaveformLoadResult::Loaded);
  CHECK_FALSE(editor.Snapshot().singleCycle);
  CHECK_FALSE(editor.Handle(TrackerAction::Play, true).singleCycle);
  editor.Handle(TrackerAction::Play, false);

  Ui2SampleSlicesController slices(waveform);
  editor.Close();
  REQUIRE(slices.OpenPath(fileSystem, "STEREO.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  CHECK_FALSE(slices.Handle(TrackerAction::Play, true).singleCycle);
}

TEST_CASE("UI2 Sample Editor keeps destructive operations typed") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(1024U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleEditorController controller(waveform);
  REQUIRE(controller.OpenLibrary(fileSystem, "VOICE.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);

  controller.SetFocus(SampleEditorViewUi2Focus::Operation);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Operation() == Ui2SampleEditorOperation::Normalize);
  controller.SetFocus(SampleEditorViewUi2Focus::Apply);
  const Ui2SampleEditorCommand apply = Tap(controller, TrackerAction::Edit);
  CHECK(apply.type ==
        Ui2SampleEditorCommandType::RequestApplyOperation);
  CHECK(apply.operation == Ui2SampleEditorOperation::Normalize);

  controller.SetFocus(SampleEditorViewUi2Focus::SaveAndLoad);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2SampleEditorCommandType::RequestSaveAndLoad);
  controller.SetFocus(SampleEditorViewUi2Focus::Discard);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2SampleEditorCommandType::RequestDiscard);
}

TEST_CASE("UI2 Sample Editor endpoints cannot cross or leave the WAV") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(64U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleEditorController controller(waveform);
  REQUIRE(controller.OpenPath(fileSystem, "TINY.WAV", true) ==
          Ui2SampleWaveformLoadResult::Loaded);
  controller.SetFocus(SampleEditorViewUi2Focus::Waveform);
  for (int move = 0; move < 100; ++move)
    Chord(controller, TrackerAction::Edit, TrackerAction::Right);
  CHECK(controller.Start() <= controller.End());
  CHECK(controller.End() == 63U);
  Chord(controller, TrackerAction::Option, TrackerAction::Right);
  for (int move = 0; move < 100; ++move)
    Chord(controller, TrackerAction::Edit, TrackerAction::Left);
  CHECK(controller.End() == controller.Start());
}

TEST_CASE("UI2 Sample Slices selects moves previews adds and deletes") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(1024U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleSlicesController controller(waveform);
  REQUIRE(controller.OpenProjectPool(fileSystem, "DEMO", "BREAK.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  std::array<std::uint32_t, Ui2SampleSlicesController::SliceCapacity> points{};
  points[0] = 0U;
  points[1] = 256U;
  points[2] = 512U;
  controller.SynchronizeSlices(points, 0x0007U);

  SampleSlicesViewUi2Snapshot snapshot = controller.Snapshot();
  CHECK(snapshot.hasSample);
  CHECK(std::strcmp(snapshot.sliceCount.data(), "03") == 0);
  CHECK(snapshot.markers.count == 3U);

  Tap(controller, TrackerAction::Right);
  CHECK(controller.SelectedSlice() == 1U);
  const Ui2SampleSlicesCommand preview =
      controller.Handle(TrackerAction::Play, true);
  CHECK(preview.type == Ui2SampleSlicesCommandType::PreviewStart);
  CHECK(preview.start == 256U);
  CHECK(preview.end == 512U);
  CHECK_FALSE(preview.singleCycle);
  CHECK(controller.Handle(TrackerAction::Play, false).type ==
        Ui2SampleSlicesCommandType::PreviewStop);

  const Ui2SampleSlicesCommand moved =
      Chord(controller, TrackerAction::Edit, TrackerAction::Right);
  CHECK(moved.type == Ui2SampleSlicesCommandType::SetSlicePoint);
  CHECK(moved.slice == 1U);
  CHECK(moved.value > 256U);

  Tap(controller, TrackerAction::Right); // slot 2
  Tap(controller, TrackerAction::Right); // slot 3, initially undefined
  const Ui2SampleSlicesCommand added = controller.AddSelectedAt(700U);
  CHECK(added.type == Ui2SampleSlicesCommandType::AddSlice);
  CHECK((controller.DefinedMask() & 0x0008U) != 0U);
  const Ui2SampleSlicesCommand deleted = controller.DeleteSelected();
  CHECK(deleted.type == Ui2SampleSlicesCommandType::DeleteSlice);
  CHECK((controller.DefinedMask() & 0x0008U) == 0U);
}

TEST_CASE("UI2 Sample Slices emits auto-slice request before replacement") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(1600U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleSlicesController controller(waveform);
  REQUIRE(controller.OpenPath(fileSystem, "LOOP.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);

  controller.SetFocus(SampleSlicesViewUi2Focus::AutoSliceCount);
  const Ui2SampleSlicesCommand count = Tap(controller, TrackerAction::Right);
  CHECK(count.type == Ui2SampleSlicesCommandType::SetAutoSliceCount);
  CHECK(count.count == 5U);
  controller.SetFocus(SampleSlicesViewUi2Focus::AutoSlice);
  const Ui2SampleSlicesCommand request = Tap(controller, TrackerAction::Edit);
  CHECK(request.type == Ui2SampleSlicesCommandType::RequestAutoSlice);
  CHECK(request.count == 5U);
  CHECK(controller.DefinedMask() == 0U);

  controller.ApplyEvenSlices(request.count);
  CHECK(controller.DefinedMask() == 0x001FU);
  CHECK(controller.SlicePoints()[1] == 320U);
  CHECK(controller.Snapshot().markers.count == 5U);
}

TEST_CASE("UI2 Sample Slices clamps synchronized and moved markers") {
  using namespace ui2;
  Config::SetImportResampler(0);
  SampleWaveFileSystem fileSystem;
  fileSystem.BuildPcm(32U);
  Ui2SampleWaveformBackend waveform;
  Ui2SampleSlicesController controller(waveform);
  REQUIRE(controller.OpenPath(fileSystem, "SHORT.WAV") ==
          Ui2SampleWaveformLoadResult::Loaded);
  std::array<std::uint32_t, Ui2SampleSlicesController::SliceCapacity> points{};
  points[0] = 99999U;
  controller.SynchronizeSlices(points, 1U);
  CHECK(controller.SlicePoints()[0] == 31U);
  for (int move = 0; move < 100; ++move)
    Chord(controller, TrackerAction::Edit, TrackerAction::Right);
  CHECK(controller.SlicePoints()[0] == 31U);
  for (int move = 0; move < 100; ++move)
    Chord(controller, TrackerAction::Edit, TrackerAction::Left);
  CHECK(controller.SlicePoints()[0] == 0U);
}

TEST_CASE("UI2 Sample controllers remain inert without a sample") {
  using namespace ui2;
  Ui2SampleWaveformBackend editorWaveform;
  Ui2SampleWaveformBackend slicesWaveform;
  Ui2SampleEditorController editor(editorWaveform);
  Ui2SampleSlicesController slices(slicesWaveform);
  CHECK_FALSE(editor.Active());
  CHECK_FALSE(editor.Snapshot().waveformReady);
  CHECK_FALSE(editor.Handle(TrackerAction::Play, true).HasValue());
  CHECK_FALSE(slices.Active());
  CHECK_FALSE(slices.Snapshot().hasSample);
  CHECK_FALSE(slices.DeleteSelected().HasValue());
}
