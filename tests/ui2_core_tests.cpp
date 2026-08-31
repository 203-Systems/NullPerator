#include "Adapters/wasm/gui/WasmUiPresenter.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"
#include "Application/UI2/Ui2ChainTranspose.h"
#include "Application/UI2/Ui2FixedText.h"
#include "Application/UI2/Ui2ModalInputGate.h"
#include "Application/UI2/Ui2NotePresentation.h"
#include "Application/UI2/Ui2SampleAdapters.h"
#include "Application/UI2/Ui2SettingsAdapters.h"
#include "Application/UI2/Ui2SettingsControllerAdapters.h"
#include "Application/UI2/Ui2VuMapping.h"
#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Render/UiRasterizer.h"
#include "UI2/Render/UiRgb565Presenter.h"
#include "UI2/Render/UiVuGradient.h"
#include "UI2/Scene/UiCommandList.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/Theme/UiThemeSchema.h"
#include "UI2/UiEngine.h"
#include "UI2/Views/Browser/UiBrowserView.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Device/UiDeviceView.h"
#include "UI2/Views/Dialog/UiDialogView.h"
#include "UI2/Views/Font/UiFontView.h"
#include "UI2/Views/Groove/UiGrooveView.h"
#include "UI2/Views/Instrument/UiInstrumentView.h"
#include "UI2/Views/Mixer/UiMixerView.h"
#include "UI2/Views/Phrase/UiPhraseView.h"
#include "UI2/Views/Project/UiProjectView.h"
#include "UI2/Views/Record/UiRecordView.h"
#include "UI2/Views/Sample/UiSampleViews.h"
#include "UI2/Views/Song/UiSongView.h"
#include "UI2/Views/Table/UiTableView.h"
#include "UI2/Views/Theme/UiThemeView.h"
#include "UI2/Views/Tracker/UiTrackerGridMetrics.h"

#include "ui2_browser_fixture.h"
#include "ui2_chain_fixture.h"
#include "ui2_device_fixture.h"
#include "ui2_groove_fixture.h"
#include "ui2_instrument_fixture.h"
#include "ui2_mixer_fixture.h"
#include "ui2_phrase_fixture.h"
#include "ui2_project_fixture.h"
#include "ui2_sample_fixture.h"
#include "ui2_song_fixture.h"
#include "ui2_table_fixture.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace {

class RecordingPresenter final : public ui2::IUiPresenter {
public:
  ui2::PresentResult Present(const ui2::UiIndexedSurface &surface,
                             const ui2::UiPalette &palette,
                             std::span<const ui2::DirtyStrip> strips) override {
    ++calls;
    pixels = surface.Pixels().data();
    firstColor = palette.Get(surface.Pixel(0, 0));
    stripCount = strips.size();
    lastStrips.fill({});
    std::copy_n(strips.begin(), std::min(strips.size(), lastStrips.size()),
                lastStrips.begin());
    return result;
  }

  ui2::PresentResult result = ui2::PresentResult::Presented;
  int calls = 0;
  const ui2::PaletteIndex *pixels = nullptr;
  ui2::Rgb888 firstColor{};
  std::size_t stripCount = 0;
  std::array<ui2::DirtyStrip, 8> lastStrips{};
};

class TestApplicationStateSource final : public ui2::IUiApplicationStateSource {
public:
  ui2::UiApplicationPage ActivePage() const override { return page; }
  std::uint32_t NowMs() const override { return nowMs; }
  ui2::UiApplicationBatteryState ReadBattery() const override {
    return {.percentage = 73, .available = true, .charging = false};
  }
  bool PersistenceSaving() const override { return persistenceSaving; }
  bool NavigationHeld() const override { return navigationHeld; }
  bool HasDialog() const override { return dialogActive; }
  Ui2DialogSnapshot DialogSnapshot() const override { return dialog; }
  std::uint32_t DialogInstanceId() const override { return dialogInstanceId; }

  ui2::UiApplicationActivityState
  CaptureSong(ui2::UiSongFrameState &state) override {
    state = {};
    state.name = {'T', 'E', 'S', 'T'};
    state.rows[songCellRow][3] = songCell;
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureChain(ui2::UiChainFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CapturePhrase(ui2::UiPhraseFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureTable(ui2::UiTableFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureInstrument(ui2::UiInstrumentFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureProject(ui2::UiProjectFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureDevice(ui2::UiDeviceFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureTheme(ui2::UiThemeFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureFont(ui2::UiFontFrameState &state) override {
    state = font;
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureBrowser(ui2::UiBrowserFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureGroove(ui2::UiGrooveFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureMixer(ui2::UiMixerFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureSampleEditor(ui2::UiSampleEditorFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureSampleSlices(ui2::UiSampleSlicesFrameState &state) override {
    state = {};
    return {};
  }
  ui2::UiApplicationActivityState
  CaptureRecord(ui2::UiRecordFrameState &state) override {
    state = {};
    return {};
  }

  ui2::UiApplicationPage page = ui2::UiApplicationPage::Song;
  ui2::UiFontFrameState font{};
  std::uint32_t nowMs = 0;
  bool persistenceSaving = false;
  bool navigationHeld = false;
  bool dialogActive = false;
  std::uint8_t songCell = 0;
  std::uint8_t songCellRow = 4U;
  std::uint32_t dialogInstanceId = 1U;
  Ui2DialogSnapshot dialog{};
};

} // namespace

namespace {

struct CommitProbe {
  static bool Commit(void *context) {
    auto &probe = *static_cast<CommitProbe *>(context);
    ++probe.calls;
    return probe.result;
  }
  int calls = 0;
  bool result = true;
};

struct Rgb565WriteProbe {
  struct Call {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t first = 0;
    std::uint16_t last = 0;
  };

  static bool Write(void *context, std::uint16_t x, std::uint16_t y,
                    std::uint16_t width, std::uint16_t height,
                    const std::uint16_t *pixels) {
    auto &probe = *static_cast<Rgb565WriteProbe *>(context);
    const std::size_t index = probe.calls++;
    if (index < probe.records.size()) {
      probe.records[index] = {x,      y,         width,
                              height, pixels[0], pixels[width * height - 1U]};
    }
    return probe.failOnCall == 0 || probe.calls != probe.failOnCall;
  }

  std::array<Call, 8> records{};
  std::size_t calls = 0;
  std::size_t failOnCall = 0;
};

template <typename Data, typename Build, typename RenderDelta>
void CheckDeltaMatchesFullFrame(const Data &previous, const Data &current,
                                Build build, RenderDelta renderDelta) {
  ui2::UiPalette palette;
  ui2::UiFrameScene previousScene;
  REQUIRE(build(previous, palette, previousScene) == ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface delta(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, delta, palette);
  delta.ClearDirty();

  ui2::UiFrameScene currentScene;
  REQUIRE(build(current, palette, currentScene) == ui2::UiBuildStatus::Built);
  renderDelta(previous, current, currentScene, delta, palette);

  ui2::UiSurfaceStorage fullStorage;
  ui2::UiIndexedSurface full(fullStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, full, palette);
  CHECK(std::equal(delta.Pixels().begin(), delta.Pixels().end(),
                   full.Pixels().begin(), full.Pixels().end()));
}

const ui2::UiCommand *FindTextCommand(const ui2::UiCommandStream &stream,
                                      std::string_view text) {
  for (const ui2::UiCommand &command : stream.commands) {
    if (command.kind != ui2::UiCommandKind::Text ||
        command.auxiliaryColor != text.size()) {
      continue;
    }
    const std::size_t begin = command.payload;
    const std::size_t end = begin + text.size();
    if (end > stream.text.size())
      continue;
    if (std::equal(text.begin(), text.end(), stream.text.begin() + begin))
      return &command;
  }
  return nullptr;
}

const ui2::UiCommand *FindLiteralTextCommand(const ui2::UiCommandStream &stream,
                                             std::string_view text) {
  for (const ui2::UiCommand &command : stream.commands) {
    if (command.kind != ui2::UiCommandKind::Text ||
        (command.parameter & 0x80U) == 0U ||
        command.auxiliaryColor != text.size()) {
      continue;
    }
    const std::size_t begin = command.payload;
    const std::size_t end = begin + text.size();
    if (end <= stream.text.size() &&
        std::equal(text.begin(), text.end(), stream.text.begin() + begin))
      return &command;
  }
  return nullptr;
}

} // namespace

TEST_CASE("UI2 geometry clips and unions signed pixel rectangles") {
  CHECK(ui2::Intersect({-4, 2, 10, 8}, ui2::RectI16::Screen()) ==
        ui2::RectI16{0, 2, 6, 8});
  CHECK(ui2::Intersect({250, 2, 4, 8}, ui2::RectI16::Screen()).Empty());
  CHECK(ui2::Union({3, 4, 5, 6}, {1, 7, 10, 2}) == ui2::RectI16{1, 4, 10, 6});
}

TEST_CASE("UI2 fixed text copies truncate and clear stale suffixes") {
  std::array<char, 5> text{'X', 'X', 'X', 'X', 'X'};
  ui2::CopyUiText(text, "ABCDEFG");
  CHECK((text == std::array<char, 5>{'A', 'B', 'C', 'D', '\0'}));

  ui2::CopyUiText(text, "OK");
  CHECK((text == std::array<char, 5>{'O', 'K', '\0', '\0', '\0'}));

  ui2::CopyUiText(text, nullptr);
  CHECK((text == std::array<char, 5>{'\0', '\0', '\0', '\0', '\0'}));

  std::array<char, 0> empty{};
  ui2::CopyUiText(empty, "IGNORED");
  CHECK(empty.empty());
}

TEST_CASE("UI2 note presentation matches the complete tracker byte domain") {
  for (std::uint16_t raw = 0U; raw <= 0xFFU; ++raw) {
    const auto value = static_cast<std::uint8_t>(raw);
    std::array<char, 5> expected{};
    if (value == NO_NOTE) {
      expected = {'-', '-', '-', '-', '\0'};
    } else if (value == NOTE_OFF) {
      expected = {'O', 'F', 'F', '\0', '\0'};
    } else if (value > HIGHEST_NOTE) {
      expected = {'?', '?', '?', '?', '\0'};
    } else {
      const char *pitch = noteNames[value % 12U];
      const int octave = static_cast<int>(value / 12U) - 2;
      if (pitch[1] == ' ')
        std::snprintf(expected.data(), expected.size(), "%c%d", pitch[0],
                      octave);
      else
        std::snprintf(expected.data(), expected.size(), "%c%c%d", pitch[0],
                      pitch[1], octave);
    }

    std::array<char, 5> actual{};
    ui2::FormatUiNote(value, actual);
    CHECK(actual == expected);
  }
}

TEST_CASE("UI2 track notes avoid the legacy shared text buffer alias") {
  struct AliasingPlayer {
    std::array<int, 8> values{73, 73, NO_NOTE, 73, 73, 73, 73, 73};
    std::array<bool, 8> muted{false, true, false, false,
                              false, false, false, false};
    std::array<char, 5> shared{};
    std::size_t rawReads = 0U;

    const char *GetPlayedNote(std::size_t) {
      shared = {'C', '#', '\0', '\0', '\0'};
      return shared.data();
    }
    const char *GetPlayedOctive(std::size_t) {
      shared = {' ', '4', '\0', '\0', '\0'};
      return shared.data();
    }
    bool IsChannelMuted(std::size_t track) const { return muted[track]; }
    int GetPlayedNoteValue(std::size_t track) {
      ++rawReads;
      return values[track];
    }
  } player;

  const char *pitch = player.GetPlayedNote(0U);
  CHECK(std::string_view(pitch) == "C#");
  const char *octave = player.GetPlayedOctive(0U);
  CHECK(pitch == octave);
  CHECK(std::string_view(pitch) == " 4");

  std::array<std::array<char, 5>, 8> notes{};
  ui2::CaptureUiTrackNotes(&player, true, notes);
  CHECK(std::string_view(notes[0].data()) == "C#4");
  CHECK(std::string_view(notes[1].data()) == "--");
  CHECK(std::string_view(notes[2].data()) == "--");
  CHECK(player.rawReads == 7U);

  ui2::CaptureUiTrackNotes(&player, false, notes);
  for (const auto &note : notes)
    CHECK(std::string_view(note.data()) == "--");
  CHECK(player.rawReads == 7U);
}

TEST_CASE("UI2 elapsed clock preserves fixed wrapping display semantics") {
  constexpr std::array<int, 10> seconds{
      -1, 0, 5, 59, 60, 61, 5999, 6000, 6001,
      std::numeric_limits<int>::max(),
  };
  for (const int value : seconds) {
    const int clamped = std::max(0, value);
    std::array<char, 6> expected{};
    std::snprintf(expected.data(), expected.size(), "%02d:%02d",
                  (clamped / 60) % 100, clamped % 60);
    std::array<char, 6> actual{};
    ui2::FormatUiElapsed(value, actual);
    CAPTURE(value);
    CHECK(actual == expected);
  }
}

TEST_CASE("UI2 mixer volume formatting clamps and clears every boundary") {
  struct Case {
    int value;
    std::array<char, 4> expected;
  };
  constexpr std::array<Case, 12> cases{{
      {std::numeric_limits<int>::min(), {'0', '\0', '\0', '\0'}},
      {-1, {'0', '\0', '\0', '\0'}},
      {0, {'0', '\0', '\0', '\0'}},
      {1, {'1', '\0', '\0', '\0'}},
      {9, {'9', '\0', '\0', '\0'}},
      {10, {'1', '0', '\0', '\0'}},
      {99, {'9', '9', '\0', '\0'}},
      {100, {'1', '0', '0', '\0'}},
      {998, {'9', '9', '8', '\0'}},
      {999, {'9', '9', '9', '\0'}},
      {1000, {'9', '9', '9', '\0'}},
      {std::numeric_limits<int>::max(), {'9', '9', '9', '\0'}},
  }};

  for (const Case &test : cases) {
    std::array<char, 4> actual{'X', 'X', 'X', 'X'};
    ui2::FormatUiVolume(test.value, actual);
    CAPTURE(test.value);
    CHECK(actual == test.expected);
  }
}

TEST_CASE("UI2 percent formatting covers uint16 boundaries and stale bytes") {
  constexpr std::array<std::uint16_t, 10> values{
      0U,   9U,    10U,   99U,     100U,
      999U, 1000U, 9999U, 10'000U, std::numeric_limits<std::uint16_t>::max(),
  };
  for (const std::uint16_t value : values) {
    std::array<char, 8> expected{};
    std::snprintf(expected.data(), expected.size(), "%u%%",
                  static_cast<unsigned>(value));
    std::array<char, 8> actual{'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
    ui2::FormatUiPercent(value, actual);
    CAPTURE(value);
    CHECK(actual == expected);
  }
}

TEST_CASE("UI2 bounded percent formatting clamps and clears compact buffers") {
  struct Case {
    std::uint8_t value;
    std::array<char, 5> expected;
  };
  constexpr std::array<Case, 8> cases{{
      {0U, {'0', '%', '\0', '\0', '\0'}},
      {1U, {'1', '%', '\0', '\0', '\0'}},
      {9U, {'9', '%', '\0', '\0', '\0'}},
      {10U, {'1', '0', '%', '\0', '\0'}},
      {99U, {'9', '9', '%', '\0', '\0'}},
      {100U, {'1', '0', '0', '%', '\0'}},
      {101U, {'1', '0', '0', '%', '\0'}},
      {255U, {'1', '0', '0', '%', '\0'}},
  }};
  for (const Case &test : cases) {
    std::array<char, 5> actual{'X', 'X', 'X', 'X', 'X'};
    ui2::FormatUiPercent100(test.value, actual);
    CAPTURE(test.value);
    CHECK(actual == test.expected);
  }
}

TEST_CASE("UI2 indexed surface owns no RGB framebuffer and clips fills") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.Clear(1);
  surface.ClearDirty();
  surface.FillRect({-2, 5, 5, 2}, 7);

  CHECK(surface.Pixel(0, 5) == 7);
  CHECK(surface.Pixel(2, 6) == 7);
  CHECK(surface.Pixel(3, 5) == 1);
  CHECK(surface.Pixel(-1, 5) == 0);
  CHECK(sizeof(storage) < 58'000);
}

TEST_CASE("UI2 indexed surface marks one exact tile for a pixel write") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.SetPixel(17, 23, 7);

  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  REQUIRE(strips.Size() == 1);
  const ui2::DirtyStrip strip = strips.Strips().front();
  CHECK(strip.x == 16);
  CHECK(strip.y == 16);
  CHECK(strip.width == 8);
  CHECK(strip.height == 8);
}

TEST_CASE(
    "UI2 rounded bubble keeps straight edges crisp and softens corners only") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.Clear(0);
  surface.ClearDirty();
  surface.FillRoundedRect({10, 12, 5, 5}, 2, 3);

  CHECK(surface.Pixel(10, 12) == 3);
  CHECK(surface.Pixel(14, 12) == 3);
  CHECK(surface.Pixel(10, 16) == 3);
  CHECK(surface.Pixel(14, 16) == 3);
  CHECK(surface.Pixel(11, 12) == 2);
  CHECK(surface.Pixel(10, 13) == 2);
  CHECK(surface.Pixel(12, 14) == 2);
}

TEST_CASE("UI2 coverage rounded fill preserves clipped corner backgrounds") {
  ui2::UiPalette palette;
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  const auto background = palette.Index(ui2::UiColorToken::SurfaceBackground);
  const auto topRightBackground = palette.Index(ui2::UiColorToken::TextDim);
  const auto bottomRightBackground =
      palette.Index(ui2::UiColorToken::SelectionActive);
  const auto fill = palette.Index(ui2::UiColorToken::CursorPrimary);
  surface.Clear(background);
  surface.SetPixel(14, 12, topRightBackground);
  surface.SetPixel(14, 16, bottomRightBackground);
  surface.ClearDirty();

  surface.FillCoverageRoundedRect({10, 12, 5, 5}, fill, palette,
                                  ui2::UiCoverage::Cursor, 1,
                                  {11, 12, 4, 5});

  CHECK(surface.Pixel(10, 12) == background);
  CHECK(surface.Pixel(10, 16) == background);
  CHECK(surface.Pixel(14, 12) ==
        palette.CoverageIndex(ui2::UiCoverage::Cursor, topRightBackground));
  CHECK(surface.Pixel(14, 16) ==
        palette.CoverageIndex(ui2::UiCoverage::Cursor,
                              bottomRightBackground));
  CHECK(surface.Pixel(11, 12) == fill);
  CHECK(surface.Pixel(12, 14) == fill);
}

TEST_CASE("UI2 dirty tiles merge a full frame into one strip") {
  ui2::UiDirtyTiles dirty;
  ui2::DirtyStripList strips;
  dirty.MarkAll();
  REQUIRE(dirty.Collect(strips));
  REQUIRE(strips.Size() == 1);
  const auto strip = strips.Strips().front();
  CHECK(strip.x == 0);
  CHECK(strip.y == 0);
  CHECK(strip.width == 240);
  CHECK(strip.height == 240);
}

TEST_CASE("UI2 dirty tiles preserve separated cursor regions") {
  ui2::UiDirtyTiles dirty;
  ui2::DirtyStripList strips;
  dirty.Mark({2, 2, 4, 4});
  dirty.Mark({40, 18, 5, 5});
  REQUIRE(dirty.Collect(strips));
  REQUIRE(strips.Size() == 2);
  CHECK(strips.Strips()[0].x == 0);
  CHECK(strips.Strips()[0].y == 0);
  CHECK(strips.Strips()[0].width == 8);
  CHECK(strips.Strips()[0].height == 8);
  CHECK(strips.Strips()[1].x == 40);
  CHECK(strips.Strips()[1].y == 16);
  CHECK(strips.Strips()[1].width == 8);
  CHECK(strips.Strips()[1].height == 8);
}

TEST_CASE("UI2 dirty tiles merge only matching runs across adjacent rows") {
  ui2::UiDirtyTiles dirty;
  ui2::DirtyStripList strips;
  dirty.Mark({0, 0, 8, 24});
  dirty.Mark({16, 0, 8, 8});
  dirty.Mark({16, 16, 8, 8});
  REQUIRE(dirty.Collect(strips));
  REQUIRE(strips.Size() == 3);
  CHECK(strips.Strips()[0].x == 0);
  CHECK(strips.Strips()[0].y == 0);
  CHECK(strips.Strips()[0].width == 8);
  CHECK(strips.Strips()[0].height == 24);
  CHECK(strips.Strips()[1].x == 16);
  CHECK(strips.Strips()[1].y == 0);
  CHECK(strips.Strips()[1].width == 8);
  CHECK(strips.Strips()[1].height == 8);
  CHECK(strips.Strips()[2].x == 16);
  CHECK(strips.Strips()[2].y == 16);
  CHECK(strips.Strips()[2].width == 8);
  CHECK(strips.Strips()[2].height == 8);
}

TEST_CASE("UI2 command lists fail closed instead of allocating") {
  ui2::UiCommandList<2> commands;
  CHECK(commands.FillRect({0, 0, 1, 1}, 1));
  CHECK(commands.FillRoundedRect({1, 1, 3, 3}, 2, 3));
  CHECK_FALSE(commands.FillRect({4, 4, 1, 1}, 4));
  CHECK(commands.Size() == 2);
  CHECK(commands.Overflowed());
}

TEST_CASE("UI2 text commands copy strings into fixed scene storage") {
  ui2::UiCommandList<2, 5> commands;
  CHECK(commands.Text({3, 4}, "ABC", 7));
  CHECK_FALSE(commands.Text({3, 4}, "DEF", 7));
  CHECK(commands.Size() == 1);
  CHECK(commands.Overflowed());
  CHECK(commands.Stream().text.size() == 3);
}

TEST_CASE("UI2 approved font renders exact 5 by 7 glyphs and clips") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.Clear(0);
  surface.ClearDirty();
  ui2::UiCommandList<2, 4> commands;
  REQUIRE(commands.Text({-1, 2}, "A", 9));
  ui2::UiRasterizer::Render(commands.Stream(), surface);

  CHECK(surface.Pixel(0, 2) == 9);
  CHECK(surface.Pixel(1, 2) == 9);
  CHECK(surface.Pixel(2, 2) == 9);
  CHECK(surface.Pixel(3, 2) == 0);
  CHECK(surface.Pixel(0, 3) == 0);
  CHECK(surface.Pixel(3, 3) == 9);
  CHECK(surface.Pixel(0, 5) == 9);
  CHECK(surface.Pixel(3, 5) == 9);
}

TEST_CASE("UI2 rasterizer preserves original corners through layer clips") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.Clear(0);
  surface.ClearDirty();
  ui2::UiCommandList<2> commands;
  REQUIRE(commands.FillRoundedRect({2, 2, 5, 5}, 2, 3));
  ui2::UiRasterizer::Render(commands.Stream(), surface, nullptr, {2, 0},
                            {5, 0, 4, 10});
  CHECK(surface.Pixel(5, 2) == 2);
  CHECK(surface.Pixel(8, 2) == 3);
}

TEST_CASE("UI2 clipped palette ramps preserve absolute row colors") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.Clear(0);
  surface.ClearDirty();
  ui2::UiCommandList<1> commands;
  REQUIRE(commands.FillVerticalPaletteRamp({3, 10, 2, 20}, 40));
  ui2::UiRasterizer::Render(commands.Stream(), surface, nullptr, {},
                            {0, 17, 20, 2});
  CHECK(surface.Pixel(3, 16) == 0);
  CHECK(surface.Pixel(3, 17) == 47);
  CHECK(surface.Pixel(4, 18) == 48);
  CHECK(surface.Pixel(3, 19) == 0);
}

TEST_CASE("UI2 semantic palette reproduces approved coverage composites") {
  ui2::UiPalette palette;
  CHECK(palette.Get(palette.Index(ui2::UiColorToken::SurfaceBackground)) ==
        ui2::Rgb888{0x03, 0x07, 0x07});
  CHECK(palette.Get(palette.CoverageIndex(
            ui2::UiCoverage::Playback,
            palette.Index(ui2::UiColorToken::SurfaceBackground))) ==
        ui2::Rgb888{0x2D, 0x65, 0x45});
  CHECK(palette.Get(palette.CoverageIndex(
            ui2::UiCoverage::Playback,
            palette.Index(ui2::UiColorToken::CursorRow))) ==
        ui2::Rgb888{0x38, 0x6E, 0x50});
}

TEST_CASE("UI2 semantic palette reproduces exact quarter coverage colors") {
  ui2::UiPalette palette;
  const ui2::PaletteIndex background =
      palette.Index(ui2::UiColorToken::DerivedVuTrack);
  CHECK(palette.Get(palette.AntialiasIndex(ui2::UiCoverage::Cursor, 1)) ==
        ui2::Rgb888{0x14, 0x42, 0x42});
  CHECK(palette.Get(palette.AntialiasIndex(ui2::UiCoverage::Cursor, 2)) ==
        ui2::Rgb888{0x24, 0x75, 0x79});
  CHECK(palette.Get(palette.AntialiasIndex(ui2::UiCoverage::Cursor, 3)) ==
        ui2::Rgb888{0x35, 0xA9, 0xB1});
  CHECK(palette.Get(palette.AntialiasIndex(ui2::UiCoverage::Playback, 3)) ==
        ui2::Rgb888{0x4F, 0xB0, 0x76});
}

TEST_CASE("UI2 user palette exposes exactly the approved semantic fields") {
  CHECK(ui2::UiPalette::kUserColorCount == 20);
  CHECK(ui2::kUiThemeColors.size() == ui2::UiPalette::kUserColorCount);
  CHECK(ui2::kUiThemeColors.front().key == "surface.bg");
  CHECK(ui2::kUiThemeColors[9].key == "selection.active");
  CHECK(ui2::kUiThemeColors.back().key == "vu.peak");
  for (std::size_t left = 0; left < ui2::kUiThemeColors.size(); ++left) {
    CHECK(static_cast<std::size_t>(ui2::kUiThemeColors[left].token) == left);
    for (std::size_t right = left + 1; right < ui2::kUiThemeColors.size();
         ++right) {
      CHECK(ui2::kUiThemeColors[left].key != ui2::kUiThemeColors[right].key);
    }
  }
}

TEST_CASE(
    "UI2 user colors remain independent while element colors regenerate") {
  ui2::UiPalette palette;
  const ui2::Rgb888 batteryLow =
      palette.Get(palette.Index(ui2::UiColorToken::BatteryLow));
  const ui2::Rgb888 vuPeak =
      palette.Get(palette.Index(ui2::UiColorToken::VuPeak));
  const ui2::Rgb888 oldCorner = palette.Get(palette.CoverageIndex(
      ui2::UiCoverage::Cursor,
      palette.Index(ui2::UiColorToken::SurfaceBackground)));

  palette.Set(ui2::UiColorToken::SystemError, {1, 2, 3});
  CHECK(palette.Get(palette.Index(ui2::UiColorToken::BatteryLow)) ==
        batteryLow);
  CHECK(palette.Get(palette.Index(ui2::UiColorToken::VuPeak)) == vuPeak);

  palette.Set(ui2::UiColorToken::CursorPrimary, {0x90, 0x20, 0x40});
  CHECK(palette.Get(palette.CoverageIndex(
            ui2::UiCoverage::Cursor,
            palette.Index(ui2::UiColorToken::SurfaceBackground))) != oldCorner);
}

TEST_CASE("UI2 screen background covers the full 240 by 240 surface") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  scene.Clear();
  scene.topHeight = 0;
  scene.bottomVisible = false;
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  const auto background = palette.Index(ui2::UiColorToken::SurfaceBackground);
  CHECK(surface.Pixel(0, 0) == background);
  CHECK(surface.Pixel(239, 239) == background);
}

TEST_CASE("UI2 tracker pages share one origin and vertical rhythm") {
  ui2::UiPhraseViewData phrase;
  ui2::UiTableViewData table;
  for (std::uint8_t row = 0; row < 15; ++row) {
    phrase.editRow = row;
    table.editRow = row;
    const std::int16_t songY = ui2::UiSongView::CursorTargetRect(0, row).y;
    CHECK(ui2::UiPhraseView::CursorTargetRect(phrase).y == songY);
    CHECK(ui2::UiTableView::CursorTargetRect(table).y == songY);
    CHECK(ui2::UiSongView::CursorTargetRect(0, row + 1).y - songY ==
          ui2::UiTrackerGridMetrics::kRowPitch);
  }
  CHECK(ui2::UiTrackerGridMetrics::kRowLabelX == 7);
  CHECK(ui2::UiTrackerGridMetrics::kSongTrackX[0] ==
        ui2::UiTrackerGridMetrics::kChainColumnX[0]);
  CHECK(ui2::UiTrackerGridMetrics::kSongTrackX[1] ==
        ui2::UiTrackerGridMetrics::kChainColumnX[1]);
  CHECK(ui2::UiTrackerGridMetrics::kColumnGap == 12);
  CHECK(ui2::UiTrackerGridMetrics::kPhraseColumnX.back() +
            ui2::UiFont5x7::TextWidth(4) <=
        240);
  CHECK(ui2::UiTrackerGridMetrics::kTableColumnX.back() +
            ui2::UiFont5x7::TextWidth(4) <=
        240);
  CHECK(ui2::UiTrackerGridMetrics::RowHighlightY(0) == 46);
  CHECK(ui2::UiTrackerGridMetrics::RowBoundsY(0) == 47);
}

TEST_CASE("UI2 tracker row band has rounded corners and crisp straight edges") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  data.editRow = 0;
  data.editTrack = 0;
  data.playbackRows.fill(-1);
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  const auto fill = palette.Index(ui2::UiColorToken::CursorRow);
  const auto corner = palette.Index(ui2::UiColorToken::DerivedCursorRowCorner);
  CHECK(surface.Pixel(5, 46) == corner);
  CHECK(surface.Pixel(6, 46) == fill);
  CHECK(surface.Pixel(5, 47) == fill);
  CHECK(surface.Pixel(217, 46) == corner);
}

TEST_CASE("UI2 Song distinguishes queued and muted playback ticks") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  data.playbackRows.fill(-1);
  data.queuedRows[0] = 2;
  data.playbackRows[1] = 3;
  data.mutedTracks[1] = true;
  data.editRow = 8;
  data.editTrack = 0;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  const ui2::RectI16 queued = ui2::UiSongView::PlaybackTickRect(0, 2);
  const ui2::RectI16 muted = ui2::UiSongView::PlaybackTickRect(1, 3);
  CHECK(surface.Pixel(queued.x, queued.y) ==
        palette.Index(ui2::UiColorToken::TextColored));
  CHECK(surface.Pixel(muted.x, muted.y) ==
        palette.Index(ui2::UiColorToken::DerivedPlaybackMuted));
}

TEST_CASE("UI2 tracker detail pages retain every playback source") {
  ui2::UiPalette palette;

  ui2::UiChainViewData chain = ui2::test::ApprovedChainFixture();
  chain.playbackRows[0] = 2;
  chain.playbackRows[3] = 11;
  chain.mutedTracks[3] = true;
  ui2::UiFrameScene chainScene;
  REQUIRE(ui2::UiChainView::Build(chain, palette, chainScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage chainStorage;
  ui2::UiIndexedSurface chainSurface(chainStorage);
  ui2::UiFrameRenderer::RenderStatic(chainScene, chainSurface, palette);
  for (const auto [row, color] :
       {std::pair<std::uint8_t, ui2::UiColorToken>{
            2U, ui2::UiColorToken::PlaybackActive},
        {11U, ui2::UiColorToken::DerivedPlaybackMuted}}) {
    const ui2::RectI16 tick = ui2::UiChainView::PlaybackTickRect(row);
    CHECK(chainSurface.Pixel(tick.x, tick.y) ==
          palette.Index(color));
  }

  ui2::UiPhraseViewData phrase = ui2::test::ApprovedPhraseFixture("note");
  phrase.playbackRows[1] = 3;
  phrase.playbackRows[6] = 12;
  phrase.mutedTracks[6] = true;
  ui2::UiFrameScene phraseScene;
  REQUIRE(ui2::UiPhraseView::Build(phrase, palette, phraseScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage phraseStorage;
  ui2::UiIndexedSurface phraseSurface(phraseStorage);
  ui2::UiFrameRenderer::RenderStatic(phraseScene, phraseSurface, palette);
  for (const auto [row, color] :
       {std::pair<std::uint8_t, ui2::UiColorToken>{
            3U, ui2::UiColorToken::PlaybackActive},
        {12U, ui2::UiColorToken::DerivedPlaybackMuted}}) {
    const ui2::RectI16 tick = ui2::UiPhraseView::PlaybackTickRect(row);
    CHECK(phraseSurface.Pixel(tick.x, tick.y) ==
          palette.Index(color));
  }

  ui2::UiTableViewData table = ui2::test::ApprovedTableFixture("phrase");
  table.playbackRows[0] = 2;
  table.automationPlaybackRows[0] = 8;
  table.automationPlaybackRows[2] = 6;
  table.selectedTrackMuted = true;
  ui2::UiFrameScene tableScene;
  REQUIRE(ui2::UiTableView::Build(table, palette, tableScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage tableStorage;
  ui2::UiIndexedSurface tableSurface(tableStorage);
  ui2::UiFrameRenderer::RenderStatic(tableScene, tableSurface, palette);
  for (const auto [group, row] :
       {std::pair<std::uint8_t, std::uint8_t>{0U, 2U}, {0U, 8U}, {2U, 6U}}) {
    const ui2::RectI16 tick = ui2::UiTableView::PlaybackTickRect(group, row);
    CHECK(tableSurface.Pixel(tick.x, tick.y) ==
          palette.Index(ui2::UiColorToken::DerivedPlaybackMuted));
  }
}

TEST_CASE("UI2 shared playback ticks prefer any audible track") {
  ui2::UiPalette palette;
  const auto checkChain = [&](std::uint8_t mutedTrack,
                              std::uint8_t audibleTrack) {
    ui2::UiChainViewData data = ui2::test::ApprovedChainFixture();
    data.playbackRows.fill(-1);
    data.playbackRows[mutedTrack] = 6;
    data.playbackRows[audibleTrack] = 6;
    data.mutedTracks[mutedTrack] = true;
    data.editRow = 6;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiChainView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 tick = ui2::UiChainView::PlaybackTickRect(6);
    CHECK(surface.Pixel(tick.x, tick.y) ==
          palette.Index(ui2::UiColorToken::PlaybackActive));
    const ui2::RectI16 cursor = ui2::UiChainView::CursorTargetRect(data);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) ==
          palette.Index(ui2::UiColorToken::PlaybackActive));
  };
  checkChain(1, 6);
  checkChain(6, 1);

  const auto checkPhrase = [&](std::uint8_t mutedTrack,
                               std::uint8_t audibleTrack) {
    ui2::UiPhraseViewData data = ui2::test::ApprovedPhraseFixture("note");
    data.playbackRows.fill(-1);
    data.playbackRows[mutedTrack] = 9;
    data.playbackRows[audibleTrack] = 9;
    data.mutedTracks[mutedTrack] = true;
    data.editRow = 9;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiPhraseView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 tick = ui2::UiPhraseView::PlaybackTickRect(9);
    CHECK(surface.Pixel(tick.x, tick.y) ==
          palette.Index(ui2::UiColorToken::PlaybackActive));
    const ui2::RectI16 cursor = ui2::UiPhraseView::CursorTargetRect(data);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) ==
          palette.Index(ui2::UiColorToken::PlaybackActive));
  };
  checkPhrase(1, 6);
  checkPhrase(6, 1);
}

TEST_CASE("UI2 muted playback stays dim under the edit cursor") {
  ui2::UiPalette palette;
  const auto muted = palette.Index(ui2::UiColorToken::DerivedPlaybackMuted);

  {
    ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
    data.playbackRows.fill(-1);
    data.editTrack = 2;
    data.editRow = 5;
    data.playbackRows[data.editTrack] = data.editRow;
    data.mutedTracks[data.editTrack] = true;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 cursor =
        ui2::UiSongView::CursorTargetRect(data.editTrack, data.editRow);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) == muted);
  }

  {
    ui2::UiChainViewData data = ui2::test::ApprovedChainFixture();
    data.playbackRows.fill(-1);
    data.editColumn = 0;
    data.editRow = 5;
    data.playbackRows[2] = data.editRow;
    data.mutedTracks[2] = true;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiChainView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 cursor = ui2::UiChainView::CursorTargetRect(data);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) == muted);
  }

  {
    ui2::UiPhraseViewData data = ui2::test::ApprovedPhraseFixture("note");
    data.playbackRows.fill(-1);
    data.editColumn = 0;
    data.editRow = 5;
    data.playbackRows[2] = data.editRow;
    data.mutedTracks[2] = true;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiPhraseView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 cursor = ui2::UiPhraseView::CursorTargetRect(data);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) == muted);
  }

  {
    ui2::UiTableViewData data = ui2::test::ApprovedTableFixture("phrase");
    data.playbackRows.fill(-1);
    data.automationPlaybackRows.fill(-1);
    data.editColumn = 0;
    data.editRow = 5;
    data.playbackRows[0] = data.editRow;
    data.selectedTrackMuted = true;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiTableView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 cursor = ui2::UiTableView::CursorTargetRect(data);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) == muted);
  }

  {
    ui2::UiGrooveViewData data = ui2::test::ApprovedGrooveFixture();
    data.editRow = 5;
    data.playbackRow = data.editRow;
    data.selectedTrackMuted = true;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiGrooveView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    ui2::UiSurfaceStorage storage;
    ui2::UiIndexedSurface surface(storage);
    ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
    const ui2::RectI16 cursor =
        ui2::UiGrooveView::CursorTargetRect(data.editRow);
    CHECK(surface.Pixel(cursor.x + 1, cursor.y + 1) == muted);
  }
}

TEST_CASE("UI2 Groove renders and delta-updates its playback tick") {
  ui2::UiPalette palette;
  ui2::UiGrooveViewData previous = ui2::test::ApprovedGrooveFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiGrooveView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiGrooveViewData current = previous;
  current.playbackRow = 6;
  current.selectedTrackMuted = true;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiGrooveView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiGrooveView::RenderDelta(previous, current, currentScene, surface,
                                 palette);

  const ui2::RectI16 tick = ui2::UiGrooveView::PlaybackTickRect(6);
  CHECK(surface.Pixel(tick.x, tick.y) ==
        palette.Index(ui2::UiColorToken::DerivedPlaybackMuted));

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 tracker headers omit Table VAL and use packed Table columns") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiTableView::Build(ui2::test::ApprovedTableFixture("instrument"),
                                  palette, scene) == ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.content.Stream(), "VAL") == nullptr);
  const auto *fx1 = FindTextCommand(scene.content.Stream(), "FX1");
  const auto *fx2 = FindTextCommand(scene.content.Stream(), "FX2");
  const auto *fx3 = FindTextCommand(scene.content.Stream(), "FX3");
  REQUIRE(fx1 != nullptr);
  REQUIRE(fx2 != nullptr);
  REQUIRE(fx3 != nullptr);
  CHECK(fx1->bounds.x == ui2::UiTrackerGridMetrics::kTableColumnX[0]);
  CHECK(fx2->bounds.x == ui2::UiTrackerGridMetrics::kTableColumnX[2]);
  CHECK(fx3->bounds.x == ui2::UiTrackerGridMetrics::kTableColumnX[4]);
}

TEST_CASE("UI2 tracker selections resolve to one clipped rounded region") {
  CHECK(ui2::UiSongView::SelectionTargetRect(1, 18, 3, 21, 16) ==
        ui2::RectI16{49, 67, 61, 39});
  CHECK(ui2::UiSongView::SelectionTargetRect(0, 0, 7, 15, 16).Empty());
  CHECK(ui2::UiChainView::SelectionTargetRect(0, 2, 1, 4) ==
        ui2::RectI16{26, 67, 44, 29});
  CHECK(ui2::UiPhraseView::SelectionTargetRect(1, 1, 4, 3) ==
        ui2::RectI16{61, 57, 114, 29});
  // Legacy Table selection may transiently report column 6. UI2 clips that
  // endpoint to the sixth visible value column without escaping the screen.
  CHECK(ui2::UiTableView::SelectionTargetRect(2, 0, 6, 15) ==
        ui2::RectI16{90, 47, 120, 159});
  CHECK(ui2::UiGrooveView::SelectionTargetRect(2, 6) ==
        ui2::RectI16{27, 66, 15, 45});
}

TEST_CASE("UI2 tracker selections use their independent theme color") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  data.editRow = 0U;
  data.editTrack = 0U;
  data.playbackRows.fill(-1);
  data.selectionVisualRect =
      ui2::UiSongView::SelectionTargetRect(0, 8, 1, 9, 0);

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  const ui2::RectI16 selection = data.selectionVisualRect;
  CHECK(surface.Pixel(selection.x, selection.y) ==
        palette.Index(ui2::UiColorToken::DerivedSelectionCorner));
  CHECK(surface.Pixel(selection.x + 1, selection.y + 1) ==
        palette.Index(ui2::UiColorToken::SelectionActive));
}

TEST_CASE("UI2 Groove selection uses the selection palette and mode bar") {
  ui2::UiGrooveViewData data = ui2::test::ApprovedGrooveFixture();
  data.selectionActive = true;
  data.selectionVisualRect = ui2::UiGrooveView::SelectionTargetRect(1, 3);

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiGrooveView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  CHECK(FindTextCommand(scene.bottom.Stream(), "SELECTION") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "MODE") != nullptr);

  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  const ui2::RectI16 selection = data.selectionVisualRect;
  CHECK(surface.Pixel(selection.x, selection.y) ==
        palette.Index(ui2::UiColorToken::DerivedSelectionCorner));
  CHECK(surface.Pixel(selection.x + 1, selection.y + 1) ==
        palette.Index(ui2::UiColorToken::SelectionActive));
}

TEST_CASE("UI2 tracker selection deltas match complete redraws") {
  {
    const ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
    ui2::UiSongViewData current = previous;
    current.selectionVisualRect =
        ui2::UiSongView::SelectionTargetRect(0, 8, 3, 11, 0);
    CheckDeltaMatchesFullFrame(previous, current, ui2::UiSongView::Build,
                               ui2::UiSongView::RenderDelta);
  }
  {
    const ui2::UiChainViewData previous = ui2::test::ApprovedChainFixture();
    ui2::UiChainViewData current = previous;
    current.selectionVisualRect =
        ui2::UiChainView::SelectionTargetRect(0, 0, 1, 5);
    CheckDeltaMatchesFullFrame(previous, current, ui2::UiChainView::Build,
                               ui2::UiChainView::RenderDelta);
  }
  {
    const ui2::UiPhraseViewData previous =
        ui2::test::ApprovedPhraseFixture("note");
    ui2::UiPhraseViewData current = previous;
    current.selectionVisualRect =
        ui2::UiPhraseView::SelectionTargetRect(1, 2, 5, 6);
    CheckDeltaMatchesFullFrame(previous, current, ui2::UiPhraseView::Build,
                               ui2::UiPhraseView::RenderDelta);
  }
  {
    const ui2::UiTableViewData previous =
        ui2::test::ApprovedTableFixture("phrase");
    ui2::UiTableViewData current = previous;
    current.selectionVisualRect =
        ui2::UiTableView::SelectionTargetRect(0, 4, 5, 9);
    CheckDeltaMatchesFullFrame(previous, current, ui2::UiTableView::Build,
                               ui2::UiTableView::RenderDelta);
  }
  {
    const ui2::UiGrooveViewData previous =
        ui2::test::ApprovedGrooveFixture();
    ui2::UiGrooveViewData current = previous;
    current.selectionActive = true;
    current.selectionVisualRect =
        ui2::UiGrooveView::SelectionTargetRect(1, 8);
    CheckDeltaMatchesFullFrame(previous, current, ui2::UiGrooveView::Build,
                               ui2::UiGrooveView::RenderDelta);
  }
}

TEST_CASE("UI2 vertical list reveals items and reconciles contextual bars") {
  ui2::UiThemeViewData previous;
  ui2::UiThemeViewData current = previous;
  current.selectedColor = 18;
  current.selectedRgb = {1U, 128U, 255U};
  current.colorComponent = 2U;
  current.scrollOffset = ui2::UiThemeView::RevealCursor(0, current);
  CHECK(current.scrollOffset == 122);

  ui2::UiPalette palette;
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiThemeView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiThemeView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  CHECK(currentScene.contentOffsetY == 122);
  CHECK(currentScene.bottomVisible);
  CHECK(FindTextCommand(currentScene.bottom.Stream(), "R") != nullptr);
  CHECK(FindTextCommand(currentScene.bottom.Stream(), "G") != nullptr);
  CHECK(FindTextCommand(currentScene.bottom.Stream(), "B") != nullptr);
  CHECK(FindTextCommand(currentScene.bottom.Stream(), "255") != nullptr);
  const ui2::RectI16 rgbCursor =
      ui2::UiChromeRenderer::BottomRgbTargetRect(2U, 255U);
  const ui2::UiCommandStream bottom = currentScene.bottom.Stream();
  const auto rgbSelection =
      std::find_if(bottom.commands.begin(), bottom.commands.end(),
                   [rgbCursor](const ui2::UiCommand &command) {
                     return command.kind ==
                                ui2::UiCommandKind::FillCoverageRoundedRect &&
                            command.bounds == rgbCursor;
                   });
  CHECK(rgbSelection != bottom.commands.end());
  ui2::UiThemeView::RenderDelta(previous, current, currentScene, surface,
                                palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  const auto mismatch =
      std::mismatch(surface.Pixels().begin(), surface.Pixels().end(),
                    expected.Pixels().begin(), expected.Pixels().end());
  CAPTURE(std::distance(surface.Pixels().begin(), mismatch.first));
  CHECK(mismatch.first == surface.Pixels().end());
  CHECK(surface.Pixel(0, 0) == palette.Index(ui2::UiColorToken::SurfaceTopBar));
  CHECK(surface.Pixel(0, 239) ==
        palette.Index(ui2::UiColorToken::SurfaceBottomBar));
}

TEST_CASE("UI2 Theme reveal keeps the final color row above its bottom bar") {
  ui2::UiThemeViewData data;
  data.selectedColor =
      static_cast<std::int8_t>(ui2::Ui2ThemeController::ColorCount - 1U);
  data.scrollOffset = ui2::UiThemeView::RevealCursor(0, data);

  const ui2::RectI16 cursor = ui2::UiThemeView::CursorTargetRect(data);
  CHECK(cursor.Bottom() - data.scrollOffset <=
        ui2::UiThemeView::kRevealBottom);

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiThemeView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.contentOffsetY == data.scrollOffset);
}

TEST_CASE("UI2 sparse coverage masks copy bounded data and decode columns") {
  ui2::UiPalette palette;
  ui2::UiContentScene scene;
  ui2::UiSceneBuilder<256, 1024> builder(scene);
  builder.Fill({10, 10, 20, 10}, ui2::UiColorToken::DerivedVuTrack);
  std::array<std::uint8_t, 11> encoded{0x00, 0x01, 0x00, 0x02, 0x04, 0x39,
                                       0xFF, 0x00, 0x05, 0x02, 0x07};
  builder.SparseCoverageMask({10, 10, 4, 10}, encoded, ui2::UiCoverage::Cursor,
                             ui2::UiColorToken::DerivedVuTrack);
  REQUIRE(builder.Ok());
  encoded.fill(0);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  surface.Clear(palette.Index(ui2::UiColorToken::SurfaceBackground));
  ui2::UiRasterizer::Render(scene.Stream(), surface, &palette);
  const ui2::PaletteIndex background =
      palette.Index(ui2::UiColorToken::DerivedVuTrack);
  CHECK(surface.Pixel(10, 10) ==
        palette.AntialiasIndex(ui2::UiCoverage::Cursor, 1));
  CHECK(surface.Pixel(11, 12) ==
        palette.AntialiasIndex(ui2::UiCoverage::Cursor, 2));
  CHECK(surface.Pixel(11, 13) ==
        palette.AntialiasIndex(ui2::UiCoverage::Cursor, 3));
  CHECK(surface.Pixel(11, 14) ==
        palette.Index(ui2::UiColorToken::CursorPrimary));
  CHECK(surface.Pixel(11, 15) ==
        palette.AntialiasIndex(ui2::UiCoverage::Cursor, 1));
  CHECK(surface.Pixel(12, 10) == background);
  CHECK(surface.Pixel(13, 15) ==
        palette.Index(ui2::UiColorToken::CursorPrimary));
  CHECK(surface.Pixel(13, 16) ==
        palette.AntialiasIndex(ui2::UiCoverage::Cursor, 2));
}

TEST_CASE("UI2 VU gradient uses fixed palette slots without RGB framebuffer") {
  ui2::UiPalette palette;
  REQUIRE(ui2::UiVuGradient::Configure(palette, 153));
  CHECK(palette.Get(ui2::UiVuGradient::IndexAt(0)) ==
        ui2::Rgb888{0xF0, 0x2E, 0x75});
  CHECK(palette.Get(ui2::UiVuGradient::IndexAt(30)).red > 0);
  CHECK(palette.Get(ui2::UiVuGradient::IndexAt(60)) ==
        ui2::Rgb888{0x00, 0xDC, 0x74});
  CHECK(palette.Get(ui2::UiVuGradient::IndexAt(152)).green >= 0xA9);
  CHECK_FALSE(ui2::UiVuGradient::Configure(palette, 154));
}

TEST_CASE("UI2 VU gradient is restored after direct dynamic palette writes") {
  ui2::UiPalette palette;
  REQUIRE(ui2::UiVuGradient::Configure(palette, 153));
  const auto middle = ui2::UiVuGradient::IndexAt(60);
  const ui2::Rgb888 expected = palette.Get(middle);

  // A cached gradient must notice any direct dynamic write and rebuild before
  // the next meter frame instead of presenting an unrelated color as audio
  // level ink.
  palette.Set(middle, {0x12, 0x34, 0x56});
  REQUIRE(palette.Get(middle) != expected);
  REQUIRE(ui2::UiVuGradient::Configure(palette, 153));
  CHECK(palette.Get(middle) == expected);

  // User theme changes also invalidate the cached ramp even when no dynamic
  // slot was touched directly.
  palette.Set(ui2::UiColorToken::VuSafe, {0x21, 0x43, 0x65});
  REQUIRE(ui2::UiVuGradient::Configure(palette, 153));
  CHECK(palette.Get(middle) == ui2::Rgb888{0x21, 0x43, 0x65});
}

TEST_CASE("UI2 bar resolver applies the documented central priority") {
  ui2::UiBottomBarModel page{.kind = ui2::UiBottomBarKind::Hidden};
  ui2::UiBottomBarModel cursor{.kind = ui2::UiBottomBarKind::Context};
  ui2::UiBottomBarModel modal{.kind = ui2::UiBottomBarKind::Actions};
  ui2::UiTrackNotesModel tracks{};
  tracks.selectedTrack = 2;
  ui2::UiBarInputs inputs{
      .pageTop = {.title = "PHRASE",
                  .meta = "3A",
                  .power = ui2::UiPowerState::Navigation,
                  .navTarget = ui2::UiNavTarget::Phrase},
      .pageDefault = page,
      .cursorContext = &cursor,
      .criticalModal = &modal,
      .editHeldTracks = &tracks,
      .editHeldNumber = true,
  };
  const ui2::UiResolvedChrome resolved = ui2::UiBarResolver::Resolve(inputs);
  CHECK(resolved.top.metaSelected);
  CHECK(resolved.top.power == ui2::UiPowerState::Navigation);
  CHECK(resolved.bottom.kind == ui2::UiBottomBarKind::Actions);
}

TEST_CASE("UI2 saving spinner replaces the top-right battery presentation") {
  ui2::UiBarScene scene;
  REQUIRE(ui2::UiChromeRenderer::BuildTop(
              {.title = "PROJECT",
               .power = ui2::UiPowerState::Saving,
               .showBatteryPercent = true,
               .batteryPercent = 73U},
              scene) == ui2::UiBuildStatus::Built);

  CHECK(FindTextCommand(scene.Stream(), "73%") == nullptr);
  const auto hasFill = [&](ui2::RectI16 bounds, ui2::UiColorToken color) {
    return std::any_of(scene.Stream().commands.begin(),
                       scene.Stream().commands.end(),
                       [&](const ui2::UiCommand &command) {
                         return command.kind == ui2::UiCommandKind::FillRect &&
                                command.bounds == bounds &&
                                command.color ==
                                    static_cast<ui2::PaletteIndex>(color);
                       });
  };
  CHECK(hasFill({216, 11, 3, 3}, ui2::UiColorToken::SystemWarning));
  CHECK(hasFill({222, 15, 3, 3}, ui2::UiColorToken::DerivedTextFaint));
  CHECK(hasFill({216, 20, 3, 3}, ui2::UiColorToken::DerivedTextFaint));
  CHECK(hasFill({210, 15, 3, 3}, ui2::UiColorToken::DerivedTextFaint));
  CHECK(scene.Size() == 5U);
}

TEST_CASE("UI2 bottom renderer bounds externally supplied item counts") {
  ui2::UiBarScene scene;
  ui2::UiBottomBarModel actions{.kind = ui2::UiBottomBarKind::Actions};
  actions.actions.actions = {"ONE", "TWO", "THREE", "FOUR"};
  actions.actions.count = 0xFFU;
  REQUIRE(ui2::UiChromeRenderer::BuildBottom(actions, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.Size() == actions.actions.actions.size());

  ui2::UiBottomBarModel context{.kind = ui2::UiBottomBarKind::Context};
  context.context.firstLine = {{{"A"}, {"B"}, {"C"}}};
  context.context.secondLine = {{{"D"}, {"E"}, {"F"}}};
  context.context.firstLineCount = 0xFFU;
  context.context.secondLineCount = 0xFFU;
  REQUIRE(ui2::UiChromeRenderer::BuildBottom(context, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.Size() == context.context.firstLine.size() +
                            context.context.secondLine.size());
}

TEST_CASE("UI2 selection mode overrides page and edit bottom bars") {
  ui2::UiBottomBarModel context{.kind = ui2::UiBottomBarKind::Context};
  ui2::UiAdjustmentLegendModel adjustment{};
  const ui2::UiResolvedChrome resolved = ui2::UiBarResolver::Resolve({
      .pageTop = {.title = "PHRASE", .meta = "3A"},
      .pageDefault = {.kind = ui2::UiBottomBarKind::Hidden},
      .cursorContext = &context,
      .enterHeldAdjustment = &adjustment,
      .selectionActive = true,
  });
  REQUIRE(resolved.bottom.kind == ui2::UiBottomBarKind::Context);
  REQUIRE(resolved.bottom.context.firstLineCount == 2U);
  CHECK(resolved.bottom.context.firstLine[0].text == "SELECTION");
  CHECK(resolved.bottom.context.firstLine[0].color ==
        ui2::UiColorToken::TextColored);
  CHECK(resolved.bottom.context.firstLine[1].text == "MODE");
}

TEST_CASE("UI2 tracker playback ticks share the song edge-tick geometry") {
  const std::uint8_t row = 7U;
  const auto chain = ui2::UiChainView::PlaybackTickRect(row);
  const auto phrase = ui2::UiPhraseView::PlaybackTickRect(row);
  const auto tableFx1 = ui2::UiTableView::PlaybackTickRect(0U, row);
  const auto tableFx2 = ui2::UiTableView::PlaybackTickRect(1U, row);
  CHECK(chain == phrase);
  CHECK(chain == tableFx1);
  CHECK(chain.width == 2);
  CHECK(chain.height == 5);
  CHECK(chain.y == ui2::UiTrackerGridMetrics::RowTextY(row) + 1);
  CHECK(tableFx2.x > tableFx1.x);
  CHECK(ui2::UiChainView::PlaybackTickRect(16U).Empty());
  CHECK(ui2::UiTableView::PlaybackTickRect(3U, row).Empty());
}

TEST_CASE("UI2 NAV targets share one movable seven by nine bubble") {
  ui2::UiBarScene scene;
  CHECK(ui2::UiChromeRenderer::BuildTop({.title = "SONG",
                                         .meta = "ONECYCAC",
                                         .power = ui2::UiPowerState::Navigation,
                                         .navTarget = ui2::UiNavTarget::Song},
                                        scene) == ui2::UiBuildStatus::Built);
  CHECK(ui2::UiChromeRenderer::BuildTop({.title = "SONG",
                                         .meta = "ONECYCAC",
                                         .power = ui2::UiPowerState::Navigation,
                                         .navTarget = ui2::UiNavTarget::Mixer},
                                        scene) == ui2::UiBuildStatus::Built);
  CHECK(ui2::UiChromeRenderer::NavTargetRect(ui2::UiNavTarget::Project) ==
        ui2::RectI16{201, 2, 7, 9});
  CHECK(ui2::UiChromeRenderer::NavTargetRect(ui2::UiNavTarget::Instrument) ==
        ui2::RectI16{225, 12, 7, 9});
  CHECK(ui2::UiChromeRenderer::NavTargetRect(ui2::UiNavTarget::Mixer) ==
        ui2::RectI16{201, 23, 7, 9});
  CHECK(ui2::UiChromeRenderer::NavTargetRect(ui2::UiNavTarget::Groove) ==
        ui2::RectI16{217, 2, 7, 9});
  CHECK(ui2::UiChromeRenderer::NavTargetRect(ui2::UiNavTarget::PhraseTable) ==
        ui2::RectI16{217, 23, 7, 9});
  CHECK(ui2::UiChromeRenderer::NavTargetRect(
            ui2::UiNavTarget::InstrumentTable) == ui2::RectI16{225, 23, 7, 9});
}

TEST_CASE("UI2 NAV maps retain SCPI and the active vertical column") {
  using Target = ui2::UiNavTarget;
  const auto song = ui2::UiChromeRenderer::NavigationMap(Target::Song);
  CHECK(song.Contains(Target::Project));
  CHECK(song.Contains(Target::Song));
  CHECK(song.Contains(Target::Chain));
  CHECK(song.Contains(Target::Phrase));
  CHECK(song.Contains(Target::Instrument));
  CHECK(song.Contains(Target::Mixer));

  const auto chain = ui2::UiChromeRenderer::NavigationMap(Target::Chain);
  CHECK(chain.Contains(Target::Song));
  CHECK(chain.Contains(Target::Chain));
  CHECK(chain.Contains(Target::Phrase));
  CHECK(chain.Contains(Target::Instrument));
  CHECK_FALSE(chain.Contains(Target::Project));
  CHECK_FALSE(chain.Contains(Target::Mixer));
  CHECK_FALSE(chain.Contains(Target::Groove));

  const auto mixer = ui2::UiChromeRenderer::NavigationMap(Target::Mixer);
  CHECK(mixer.Contains(Target::Project));
  CHECK(mixer.Contains(Target::Song));
  CHECK(mixer.Contains(Target::Chain));
  CHECK(mixer.Contains(Target::Phrase));
  CHECK(mixer.Contains(Target::Instrument));
  CHECK(mixer.Contains(Target::Mixer));

  const auto instrument =
      ui2::UiChromeRenderer::NavigationMap(Target::Instrument);
  CHECK(instrument.Contains(Target::Song));
  CHECK(instrument.Contains(Target::Chain));
  CHECK(instrument.Contains(Target::Phrase));
  CHECK(instrument.Contains(Target::Instrument));
  CHECK(instrument.Contains(Target::InstrumentTable));
  CHECK_FALSE(instrument.Contains(Target::PhraseTable));

  const auto phraseTable =
      ui2::UiChromeRenderer::NavigationMap(Target::PhraseTable);
  CHECK(phraseTable.Contains(Target::Song));
  CHECK(phraseTable.Contains(Target::Chain));
  CHECK(phraseTable.Contains(Target::Phrase));
  CHECK(phraseTable.Contains(Target::Instrument));
  CHECK(phraseTable.Contains(Target::Groove));
  CHECK(phraseTable.Contains(Target::PhraseTable));
  CHECK_FALSE(phraseTable.Contains(Target::InstrumentTable));

  const auto instrumentTable =
      ui2::UiChromeRenderer::NavigationMap(Target::InstrumentTable);
  CHECK(instrumentTable.Contains(Target::Song));
  CHECK(instrumentTable.Contains(Target::Chain));
  CHECK(instrumentTable.Contains(Target::Phrase));
  CHECK(instrumentTable.Contains(Target::Instrument));
  CHECK(instrumentTable.Contains(Target::InstrumentTable));
  CHECK_FALSE(instrumentTable.Contains(Target::PhraseTable));

  const auto project = ui2::UiChromeRenderer::NavigationMap(Target::Project);
  CHECK(project.Contains(Target::Project));
  CHECK(project.Contains(Target::Song));
  CHECK(project.Contains(Target::Chain));
  CHECK(project.Contains(Target::Phrase));
  CHECK(project.Contains(Target::Instrument));
  CHECK(project.Contains(Target::Mixer));
}

TEST_CASE("UI2 pages without an explicit NAV target do not inherit Song map") {
  ui2::UiBarScene scene;
  REQUIRE(ui2::UiChromeRenderer::BuildTop(
              {.title = "DEVICE", .power = ui2::UiPowerState::Navigation},
              scene) == ui2::UiBuildStatus::Built);
  // The title is the only command. No selection bubble and no P/S/C/M glyph
  // is invented for a page whose navigation map has not been approved.
  CHECK(scene.Size() == 1);
  CHECK(ui2::UiChromeRenderer::NavigationMap(ui2::UiNavTarget::None).visible ==
        0U);
  CHECK(ui2::UiChromeRenderer::NavTargetRect(ui2::UiNavTarget::None).Empty());

  const ui2::UiResolvedChrome resolved = ui2::UiBarResolver::Resolve(
      {.pageTop = {.title = "THEME",
                   .power = ui2::UiPowerState::Navigation}});
  CHECK(resolved.top.power == ui2::UiPowerState::Navigation);
  CHECK(resolved.top.navTarget == ui2::UiNavTarget::None);
}

TEST_CASE("UI2 fixed-point easing is nonlinear and lands exactly") {
  ui2::UiMotionTrack track;
  track.Start(0, 240, 1'000, 180);
  CHECK(track.Sample(1'000) == 0);
  CHECK(track.Sample(1'045) > 60); // Ease-out is ahead of linear at 25%.
  CHECK(track.Sample(1'090) > 120);
  CHECK(track.Sample(1'180) == 240);
  CHECK_FALSE(track.Active(1'180));
}

TEST_CASE("UI2 fixed-point motion remains continuous across millis wrap") {
  ui2::UiMotionTrack track;
  track.Start(0, 100, 0xFFFFFFF0U, 40);
  CHECK(track.Active(0xFFFFFFFAU));
  CHECK(track.Sample(0xFFFFFFFAU) > 0);
  CHECK(track.Active(5U));
  CHECK(track.Sample(5U) > 50);
  CHECK(track.Sample(24U) == 100);
  CHECK_FALSE(track.Active(24U));
}

TEST_CASE("UI2 cursor roles animate independently for dual-cursor input") {
  ui2::UiCursorAnimatorSet cursors;
  cursors.Snap(ui2::UiCursorRole::TopMeta, {83, 9, 15, 9}, 100);
  cursors.Snap(ui2::UiCursorRole::BottomTrack, {68, 211, 15, 9}, 100);
  cursors.Retarget(ui2::UiCursorRole::TopMeta, {95, 9, 15, 9}, 100);
  cursors.Retarget(ui2::UiCursorRole::BottomTrack, {98, 211, 15, 9}, 100);

  CHECK(cursors.Sample(ui2::UiCursorRole::TopMeta, 130).x > 86);
  CHECK(cursors.Sample(ui2::UiCursorRole::BottomTrack, 130).x > 75);
  CHECK(cursors.Sample(ui2::UiCursorRole::TopMeta, 220) ==
        ui2::RectI16{95, 9, 15, 9});
  CHECK(cursors.Sample(ui2::UiCursorRole::BottomTrack, 220) ==
        ui2::RectI16{98, 211, 15, 9});
}

TEST_CASE("UI2 cursor retarget continues from its current visual position") {
  ui2::UiAnimatedRect cursor;
  cursor.Snap({20, 40, 15, 9}, 1'000);
  cursor.Retarget({120, 140, 15, 9}, 1'000, 120);
  const ui2::RectI16 interrupted = cursor.Sample(1'040);
  CHECK(interrupted.x > 20);
  CHECK(interrupted.x < 120);
  CHECK(interrupted.y > 40);
  CHECK(interrupted.y < 140);

  cursor.Retarget({60, 80, 15, 9}, 1'040, 120);
  CHECK(cursor.Sample(1'040) == interrupted);
  CHECK(cursor.Sample(1'160) == ui2::RectI16{60, 80, 15, 9});
}

TEST_CASE("UI2 approved Song fixture fits fixed scene buffers") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(ui2::test::ApprovedSongFixture(), palette,
                                 scene) == ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.top.Overflowed());
  CHECK_FALSE(scene.content.Overflowed());
  CHECK_FALSE(scene.bottom.Overflowed());
  CHECK(scene.content.Size() < 200);

  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  CHECK(surface.Pixel(0, 0) == palette.Index(ui2::UiColorToken::SurfaceTopBar));
  CHECK(surface.Pixel(5, 34) ==
        palette.Index(ui2::UiColorToken::SurfaceBackground));
  CHECK(surface.Pixel(5, 128) == palette.Index(ui2::UiColorToken::CursorRow));
  CHECK(surface.Pixel(219, 47) ==
        palette.Index(ui2::UiColorToken::DerivedVuTrack));
  const ui2::RectI16 cursor = ui2::UiSongView::CursorTargetRect(0, 8);
  CHECK(surface.Pixel(cursor.x + 1, cursor.y + 4) ==
        palette.Index(ui2::UiColorToken::PlaybackActive));
}

TEST_CASE("UI2 engine calls one presenter and clears dirt only after success") {
  ui2::UiEngineStorage storage;
  RecordingPresenter presenter;
  ui2::UiEngine engine(storage, presenter);
  engine.Palette().Set(4, {1, 2, 3});
  ui2::UiCommandList<4> commands;
  REQUIRE(commands.FillRect({0, 0, 240, 34}, 4));

  CHECK(engine.RenderAndPresent(commands) == ui2::PresentResult::Presented);
  CHECK(presenter.calls == 1);
  CHECK(presenter.firstColor == ui2::Rgb888{1, 2, 3});
  CHECK(presenter.stripCount == 1);
  CHECK_FALSE(engine.Surface().DirtyTiles().Any());
  CHECK(engine.PresentDirty() == ui2::PresentResult::Deferred);
  CHECK(presenter.calls == 1);

  presenter.result = ui2::PresentResult::Deferred;
  engine.Surface().SetPixel(20, 20, 4);
  CHECK(engine.PresentDirty() == ui2::PresentResult::Deferred);
  CHECK(engine.Surface().DirtyTiles().Any());
}

TEST_CASE("UI2 WASM presenter converts only dirty strips and commits once") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiPalette palette;
  surface.Clear(palette.Index(ui2::UiColorToken::SurfaceBackground));
  surface.ClearDirty();
  surface.FillRect({8, 8, 8, 8},
                   palette.Index(ui2::UiColorToken::CursorPrimary));
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::array<std::uint8_t, 240 * 240 * 4> rgba{};
  rgba.fill(0xA5);
  CommitProbe probe;
  WasmUiPresenter presenter(rgba.data(), rgba.size(), &CommitProbe::Commit,
                            &probe);
  CHECK(presenter.Present(surface, palette, strips.Strips()) ==
        ui2::PresentResult::Presented);
  CHECK(probe.calls == 1);
  const std::size_t inside = (8U * 240U + 8U) * 4U;
  CHECK(rgba[inside] == 0x45);
  CHECK(rgba[inside + 1] == 0xDC);
  CHECK(rgba[inside + 2] == 0xE8);
  CHECK(rgba[inside + 3] == 0xFF);
  CHECK(rgba[0] == 0xA5);

  probe.result = false;
  CHECK(presenter.Present(surface, palette, strips.Strips()) ==
        ui2::PresentResult::Deferred);
  CHECK(probe.calls == 2);
}

TEST_CASE("UI2 RGB565 presenter chunks dirty strips without a framebuffer") {
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiPalette palette;
  const auto field = palette.Index(ui2::UiColorToken::SurfaceBackground);
  const auto cursor = palette.Index(ui2::UiColorToken::CursorPrimary);
  surface.Clear(field);
  surface.SetPixel(4, 5, cursor);

  const std::array<ui2::DirtyStrip, 1> strips{{{4, 5, 3, 10}}};
  std::array<std::uint16_t, ui2::UiRgb565Presenter::kTransferPixels> transfer{};
  Rgb565WriteProbe probe;
  ui2::UiRgb565Presenter presenter(
      transfer.data(), transfer.size(), &Rgb565WriteProbe::Write, &probe,
      ui2::UiRgb565ByteOrder::MostSignificantByteFirst);
  CHECK(presenter.Present(surface, palette, strips) ==
        ui2::PresentResult::Presented);
  REQUIRE(probe.calls == 1);
  CHECK(probe.records[0].x == 4);
  CHECK(probe.records[0].y == 5);
  CHECK(probe.records[0].width == 3);
  CHECK(probe.records[0].height == 10);

  const std::uint16_t cursor565 = palette.Rgb565(cursor);
  const std::uint16_t field565 = palette.Rgb565(field);
  CHECK(probe.records[0].first ==
        static_cast<std::uint16_t>((cursor565 >> 8U) | (cursor565 << 8U)));
  CHECK(probe.records[0].last ==
        static_cast<std::uint16_t>((field565 >> 8U) | (field565 << 8U)));

  Rgb565WriteProbe nativeProbe;
  ui2::UiRgb565Presenter nativePresenter(
      transfer.data(), transfer.size(), &Rgb565WriteProbe::Write,
      &nativeProbe, ui2::UiRgb565ByteOrder::Native);
  CHECK(nativePresenter.Present(surface, palette, strips) ==
        ui2::PresentResult::Presented);
  REQUIRE(nativeProbe.calls == 1);
  CHECK(nativeProbe.records[0].first == cursor565);
  CHECK(nativeProbe.records[0].last == field565);

  probe = {};
  probe.failOnCall = 1;
  CHECK(presenter.Present(surface, palette, strips) ==
        ui2::PresentResult::Deferred);
  CHECK(probe.calls == 1);

  std::array<std::uint16_t, 1> undersized{};
  Rgb565WriteProbe rejectedProbe;
  ui2::UiRgb565Presenter rejected(undersized.data(), undersized.size(),
                                  &Rgb565WriteProbe::Write, &rejectedProbe,
                                  ui2::UiRgb565ByteOrder::Native);
  CHECK(rejected.Present(surface, palette, strips) ==
        ui2::PresentResult::Failed);
  CHECK(rejectedProbe.calls == 0);
}

TEST_CASE("UI2 application runtime consumes the pure state-source boundary") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;

  CHECK(runtime.Present(source) == ui2::PresentResult::Presented);
  CHECK(presenter.calls == 1);

  source.page = ui2::UiApplicationPage::None;
  CHECK(runtime.Present(source) == ui2::PresentResult::Deferred);
  CHECK(presenter.calls == 1);
}

TEST_CASE("UI2 runtime repaints a stable modal after live base deltas") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;
  source.dialogActive = true;
  source.dialog.kind = ui2::UiDialogKind::Message;
  source.dialog.SetTitle("LIVE ERROR");
  source.dialog.PushAction(ui2::UiDialogAction::Ok);

  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  REQUIRE(presenter.pixels != nullptr);
  const ui2::RectI16 damage =
      ui2::UiDialogView::DamageRect(ui2::UiDialogKind::Message);
  const auto hashDamage = [&]() {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::int16_t y = damage.y; y < damage.Bottom(); ++y) {
      for (std::int16_t x = damage.x; x < damage.Right(); ++x) {
        hash ^= presenter.pixels[static_cast<std::size_t>(y) *
                                     ui2::kScreenWidth +
                                 static_cast<std::size_t>(x)];
        hash *= 1099511628211ULL;
      }
    }
    return hash;
  };
  const std::uint64_t before = hashDamage();

  // Row 04 / track 4 lies under the modal. Live playback or model changes can
  // still invalidate it while the modal is open.
  source.songCell = 1U;
  source.nowMs = 1U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  CHECK(hashDamage() == before);
}

TEST_CASE("UI2 runtime repaints stable feedback after live base deltas") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;
  source.dialogActive = true;
  source.dialog.kind = ui2::UiDialogKind::Feedback;
  source.dialog.tone = ui2::UiDialogTone::Error;
  source.dialog.SetTitle("NO FREE TABLE");
  source.songCellRow = 14U;

  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  REQUIRE(presenter.pixels != nullptr);
  const ui2::RectI16 damage =
      ui2::UiDialogView::DamageRect(ui2::UiDialogKind::Feedback);
  const auto hashDamage = [&]() {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::int16_t y = damage.y; y < damage.Bottom(); ++y) {
      for (std::int16_t x = damage.x; x < damage.Right(); ++x) {
        hash ^= presenter.pixels[static_cast<std::size_t>(y) *
                                     ui2::kScreenWidth +
                                 static_cast<std::size_t>(x)];
        hash *= 1099511628211ULL;
      }
    }
    return hash;
  };
  const std::uint64_t before = hashDamage();

  source.songCell = 1U;
  source.nowMs = 1U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  CHECK(hashDamage() == before);
}

TEST_CASE("UI2 Font runtime animates the cursor between CASE and FONT") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;
  source.page = ui2::UiApplicationPage::Font;

  const ui2::RectI16 caseTarget =
      ui2::UiFontView::CursorTargetRect(ui2::UiFontCursor::TextCase);
  const ui2::RectI16 fontTarget =
      ui2::UiFontView::CursorTargetRect(ui2::UiFontCursor::Browse);
  CHECK(caseTarget == ui2::RectI16{7, 53, 226, 9});
  CHECK(fontTarget == ui2::RectI16{7, 85, 226, 9});

  const auto checkCursorPixel = [&](ui2::RectI16 rect) {
    REQUIRE(presenter.pixels != nullptr);
    constexpr std::int16_t sampleX = 70;
    CHECK(presenter.pixels[static_cast<std::size_t>(rect.y + rect.height / 2) *
                               ui2::kScreenWidth +
                           sampleX] ==
          static_cast<ui2::PaletteIndex>(ui2::UiColorToken::CursorPrimary));
  };

  source.font.cursor = ui2::UiFontCursor::TextCase;
  source.nowMs = 0U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(caseTarget);

  ui2::UiAnimatedRect expected;
  expected.Snap(caseTarget, 0U);
  expected.Retarget(fontTarget, 1U, 120U);
  source.font.cursor = ui2::UiFontCursor::Browse;
  source.nowMs = 1U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(expected.Sample(source.nowMs));

  source.nowMs = 61U;
  const ui2::RectI16 fontMidpoint = expected.Sample(source.nowMs);
  CHECK(fontMidpoint == ui2::RectI16{7, 80, 226, 9});
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(fontMidpoint);

  source.nowMs = 121U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(fontTarget);
  source.nowMs = 122U;
  CHECK(runtime.Present(source) == ui2::PresentResult::Deferred);

  expected.Retarget(caseTarget, 123U, 120U);
  source.font.cursor = ui2::UiFontCursor::TextCase;
  source.nowMs = 123U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(expected.Sample(source.nowMs));

  source.nowMs = 183U;
  const ui2::RectI16 caseMidpoint = expected.Sample(source.nowMs);
  CHECK(caseMidpoint.y > caseTarget.y);
  CHECK(caseMidpoint.y < fontTarget.y);
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(caseMidpoint);

  source.nowMs = 243U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  checkCursorPixel(caseTarget);
  source.nowMs = 244U;
  CHECK(runtime.Present(source) == ui2::PresentResult::Deferred);
}

TEST_CASE("UI2 persistence status overrides navigation and battery chrome") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;

  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  source.persistenceSaving = true;
  source.nowMs = 1U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  REQUIRE(presenter.pixels != nullptr);
  const auto topRightPixel = [&]() {
    return presenter.pixels[11U * ui2::kScreenWidth + 216U];
  };
  CHECK(topRightPixel() ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::SystemWarning));

  // Holding NAV cannot replace a persistence indicator that must remain
  // visible until the blocking write completes.
  source.navigationHeld = true;
  source.nowMs = 2U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  CHECK(topRightPixel() ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::SystemWarning));

  source.persistenceSaving = false;
  source.nowMs = 3U;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  CHECK(topRightPixel() !=
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::SystemWarning));
}

TEST_CASE("UI2 runtime applies all persisted semantic theme colors globally") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;
  std::array<std::uint32_t, ui2::UiPalette::kUserColorCount> colors{};
  for (std::size_t index = 0; index < colors.size(); ++index)
    colors[index] = static_cast<std::uint32_t>(0x102030U + index * 0x010203U);

  runtime.ApplyThemeColors(colors);
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  const std::uint32_t top = colors[static_cast<std::size_t>(
      ui2::UiColorToken::SurfaceTopBar)];
  CHECK(presenter.firstColor ==
        ui2::Rgb888{static_cast<std::uint8_t>(top >> 16U),
                    static_cast<std::uint8_t>(top >> 8U),
                    static_cast<std::uint8_t>(top)});
}

TEST_CASE("UI2 held NAV persists through page changes without page motion") {
  RecordingPresenter presenter;
  ui2::UiApplicationRuntime runtime(presenter);
  TestApplicationStateSource source;
  source.navigationHeld = true;

  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  source.page = ui2::UiApplicationPage::Chain;
  source.nowMs = 10;
  // The destination page replaces the source immediately.
  CHECK(runtime.Present(source) == ui2::PresentResult::Presented);
  const int pageSwitchCalls = presenter.calls;

  // The small navigation cursor keeps its independent movement animation;
  // these frames do not move or redraw the page content as a sliding layer.
  source.nowMs = 90;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  CHECK(presenter.calls == pageSwitchCalls + 1);
  source.nowMs = 190;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);

  // The true key-up changes chrome state once; a repeated idle frame cannot
  // resurrect the NAV map or leave the transition stuck.
  source.navigationHeld = false;
  source.nowMs = 191;
  REQUIRE(runtime.Present(source) == ui2::PresentResult::Presented);
  const int releaseCalls = presenter.calls;
  source.nowMs = 192;
  CHECK(runtime.Present(source) == ui2::PresentResult::Deferred);
  CHECK(presenter.calls == releaseCalls);
}

TEST_CASE("UI2 VU mapping is bounded monotonic and integer only") {
  CHECK(ui2::Ui2VuTopFromAmplitude(0U) == ui2::Ui2VuMeterHeight);
  CHECK(ui2::Ui2VuTopFromAmplitude(32U) == ui2::Ui2VuMeterHeight);
  CHECK(ui2::Ui2VuTopFromAmplitude(33U) == ui2::Ui2VuMeterHeight);
  CHECK(ui2::Ui2VuTopFromAmplitude(32699U) == 1U);
  CHECK(ui2::Ui2VuTopFromAmplitude(32700U) == 0U);
  CHECK(ui2::Ui2VuTopFromAmplitude(
            std::numeric_limits<std::uint16_t>::max()) == 0U);

  std::uint8_t previous = ui2::Ui2VuTopFromAmplitude(0U);
  bool monotonic = true;
  std::uint32_t violation = 0U;
  for (std::uint32_t amplitude = 1U;
       amplitude <= std::numeric_limits<std::uint16_t>::max(); ++amplitude) {
    const std::uint8_t top = ui2::Ui2VuTopFromAmplitude(
        static_cast<std::uint16_t>(amplitude));
    if (top > previous) {
      monotonic = false;
      violation = amplitude;
      break;
    }
    previous = top;
  }
  CAPTURE(violation);
  CHECK(monotonic);
}

TEST_CASE("UI2 firmware runtime keeps a fixed bounded memory footprint") {
  // 64-bit Host is the larger layout; the ESP32-S3 build uses 32-bit pointers.
  CHECK(sizeof(ui2::UiApplicationRuntime) < 77'000);
  CHECK(sizeof(ui2::UiRgb565Presenter) <= 64);
  CHECK(ui2::UiRgb565Presenter::kTransferPixels * sizeof(std::uint16_t) ==
        11'520);
}

TEST_CASE("UI2 battery sampling is bounded to 1 Hz and refreshes after play") {
  ui2::detail::UiBatterySampleGate gate;
  CHECK(gate.ShouldSample(false, 0U));
  CHECK_FALSE(gate.ShouldSample(false, 0U));
  CHECK_FALSE(gate.ShouldSample(false, 999U));
  CHECK(gate.ShouldSample(false, 1'000U));

  CHECK_FALSE(gate.ShouldSample(true, 1'001U));
  CHECK_FALSE(gate.ShouldSample(true, 5'000U));
  // The first idle frame after playback must observe charging/battery changes
  // immediately instead of waiting for the previous idle sample deadline.
  CHECK(gate.ShouldSample(false, 5'000U));
  CHECK_FALSE(gate.ShouldSample(false, 5'999U));
  CHECK(gate.ShouldSample(false, 6'000U));

  // Unsigned elapsed time keeps the same policy across the 32-bit clock wrap.
  gate = {};
  CHECK(gate.ShouldSample(false, UINT32_MAX - 500U));
  CHECK_FALSE(gate.ShouldSample(false, 100U));
  CHECK(gate.ShouldSample(false, 500U));
}

TEST_CASE("UI2 modal input gate retains consumed chord bits until key-up") {
  Ui2ModalInputGate gate;
  constexpr std::uint16_t edit = 1U << 5U;
  constexpr std::uint16_t enter = 1U << 6U;
  constexpr std::uint16_t left = 1U;

  gate.OnButtonDown(edit, false);
  CHECK(gate.EffectiveMask(edit, false) == edit);

  gate.OnButtonDown(enter, true);
  gate.OnButtonDown(left, true);
  const std::uint16_t chord = edit | enter | left;
  CHECK(gate.EffectiveMask(chord, true) == 0U);
  CHECK(gate.DispatchMask(chord, true) == chord);
  // The modal dismissed on Enter-down. Edit belonged to the base page, while
  // both modal-consumed keys stay hidden independently until released.
  CHECK(gate.EffectiveMask(chord, false) == edit);
  CHECK(gate.DispatchMask(chord, false) == edit);

  gate.OnButtonUp(enter);
  CHECK(gate.EffectiveMask(edit | left, false) == edit);
  gate.OnButtonUp(left);
  CHECK(gate.EffectiveMask(edit, false) == edit);
  gate.OnButtonUp(edit);
  CHECK(gate.EffectiveMask(0U, false) == 0U);
}

TEST_CASE("UI2 region rendering restores exact pixels without a backbuffer") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(ui2::test::ApprovedSongFixture(), palette,
                                 scene) == ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(scene, expected, palette);

  ui2::UiSurfaceStorage actualStorage;
  ui2::UiIndexedSurface actual(actualStorage);
  ui2::UiFrameRenderer::RenderStatic(scene, actual, palette);
  actual.FillRect({0, 0, 30, 240},
                  palette.Index(ui2::UiColorToken::BatteryLow));
  ui2::UiFrameRenderer::RenderRegion(scene, actual, palette, {0, 0, 30, 240});

  CHECK(std::equal(actual.Pixels().begin(), actual.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Song damage geometry keeps stereo VU channels separate") {
  CHECK(ui2::UiSongView::VuDamageRect(0) == ui2::RectI16{219, 47, 7, 153});
  CHECK(ui2::UiSongView::VuDamageRect(1) == ui2::RectI16{228, 47, 7, 153});
  CHECK(ui2::UiSongView::VuDamageRect(0).Right() <
        ui2::UiSongView::VuDamageRect(1).x);
  CHECK(ui2::Intersect(ui2::UiSongView::RowDamageRect(15),
                       ui2::RectI16::Screen()) ==
        ui2::UiSongView::RowDamageRect(15));
}

TEST_CASE("UI2 Song LIVE title is part of the shared delta-rendered scene") {
  const ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
  ui2::UiSongViewData current = previous;
  current.liveMode = true;

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(current, palette, scene) ==
          ui2::UiBuildStatus::Built);
  constexpr std::array<char, 4> live{'L', 'I', 'V', 'E'};
  const auto topText = scene.top.Stream().text;
  CHECK(std::search(topText.begin(), topText.end(), live.begin(), live.end()) !=
        topText.end());

  CheckDeltaMatchesFullFrame(previous, current, ui2::UiSongView::Build,
                             ui2::UiSongView::RenderDelta);
}

TEST_CASE("UI2 Song two-option mode selector has no carousel arrows") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  data.modeFocus = true;
  data.liveMode = false;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);

  const ui2::UiCommand *song = FindTextCommand(scene.bottom.Stream(), "SONG");
  const ui2::UiCommand *live = FindTextCommand(scene.bottom.Stream(), "LIVE");
  REQUIRE(song != nullptr);
  REQUIRE(live != nullptr);
  CHECK(song->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextColored));
  CHECK(live->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextDim));
  CHECK(FindTextCommand(scene.bottom.Stream(), "<") == nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), ">") == nullptr);
}

TEST_CASE("UI2 Song bottom bar does not select a track by default") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  data.editTrack = 4U;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);

  CHECK(std::none_of(scene.bottom.Stream().commands.begin(),
                     scene.bottom.Stream().commands.end(),
                     [](const ui2::UiCommand &command) {
                       return command.kind ==
                              ui2::UiCommandKind::FillCoverageRoundedRect;
                     }));
}

TEST_CASE("UI2 Song selection replaces the track-note bottom bar") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  data.selectionActive = true;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);

  const ui2::UiCommand *selection =
      FindTextCommand(scene.bottom.Stream(), "SELECTION");
  const ui2::UiCommand *mode = FindTextCommand(scene.bottom.Stream(), "MODE");
  REQUIRE(selection != nullptr);
  REQUIRE(mode != nullptr);
  CHECK(selection->bounds.x == 79);
  CHECK(FindTextCommand(scene.bottom.Stream(), "T1") == nullptr);
}

TEST_CASE("UI2 Song renders chain zero as normal data rather than empty") {
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  for (auto &row : data.rows)
    row.fill(0xFFU);
  data.rows[0][0] = 0x00U;
  data.editRow = 1U;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);

  const auto &stream = scene.content.Stream();
  const ui2::UiCommand *chainZero = nullptr;
  for (const ui2::UiCommand &command : stream.commands) {
    if (command.kind != ui2::UiCommandKind::Text ||
        command.bounds.x != ui2::UiTrackerGridMetrics::kSongTrackX[0] ||
        command.auxiliaryColor != 2U) {
      continue;
    }
    const std::size_t begin = command.payload;
    if (begin + 2U <= stream.text.size() && stream.text[begin] == '0' &&
        stream.text[begin + 1U] == '0') {
      chainZero = &command;
      break;
    }
  }
  REQUIRE(chainZero != nullptr);
  CHECK(chainZero->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextNormal));
}

TEST_CASE("UI2 Song delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette deltaPalette;
  ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiSongView::Build(previous, deltaPalette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface deltaSurface(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, deltaSurface, deltaPalette);
  deltaSurface.ClearDirty();

  ui2::UiSongViewData current = previous;
  current.name = "NEXTSONG";
  current.elapsed = "00:09";
  current.editRow = 9;
  current.editTrack = 3;
  current.rows[4][2] = 0x7A;
  current.notes[5] = "A#4";
  current.playbackRows[1] = 12;
  current.queuedRows[4] = 11;
  current.mutedTracks[1] = true;
  current.vuLevelTop = {41, 9};
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSongView::Build(current, deltaPalette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSongView::RenderDelta(previous, current, currentScene, deltaSurface,
                               deltaPalette);

  ui2::UiPalette fullPalette;
  ui2::UiFrameScene fullScene;
  REQUIRE(ui2::UiSongView::Build(current, fullPalette, fullScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage fullStorage;
  ui2::UiIndexedSurface fullSurface(fullStorage);
  ui2::UiFrameRenderer::RenderStatic(fullScene, fullSurface, fullPalette);

  CHECK(std::equal(deltaSurface.Pixels().begin(), deltaSurface.Pixels().end(),
                   fullSurface.Pixels().begin(), fullSurface.Pixels().end()));
  CHECK(deltaSurface.DirtyTiles().Any());
}

TEST_CASE("UI2 Song animated cursor delta matches the same full visual frame") {
  ui2::UiPalette deltaPalette;
  ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiSongView::Build(previous, deltaPalette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface deltaSurface(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, deltaSurface, deltaPalette);
  deltaSurface.ClearDirty();

  ui2::UiSongViewData current = previous;
  current.editRow = 3;
  current.editTrack = 5;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {91, 85, 15, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSongView::Build(current, deltaPalette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSongView::RenderDelta(previous, current, currentScene, deltaSurface,
                               deltaPalette);

  ui2::UiPalette fullPalette;
  ui2::UiFrameScene fullScene;
  REQUIRE(ui2::UiSongView::Build(current, fullPalette, fullScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage fullStorage;
  ui2::UiIndexedSurface fullSurface(fullStorage);
  ui2::UiFrameRenderer::RenderStatic(fullScene, fullSurface, fullPalette);

  CHECK(std::equal(deltaSurface.Pixels().begin(), deltaSurface.Pixels().end(),
                   fullSurface.Pixels().begin(), fullSurface.Pixels().end()));
  CHECK(deltaSurface.Pixel(current.cursorVisualRect.x + 7,
                           current.cursorVisualRect.y + 4) ==
        deltaPalette.Index(ui2::UiColorToken::CursorPrimary));
  CHECK(ui2::UiSongView::CursorTargetRect(5, 3) ==
        ui2::RectI16{141, 77, 15, 9});
}

TEST_CASE("UI2 Song idle is clean and a cursor move stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiSongView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiSongView::RenderDelta(previous, previous, previousScene, surface,
                               palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiSongViewData current = previous;
  current.editTrack = 1;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSongView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSongView::RenderDelta(previous, current, currentScene, surface,
                               palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 5'000);
  CHECK(transferredPixels < 240U * 240U / 10U);
}

TEST_CASE("UI2 Phrase delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette deltaPalette;
  ui2::UiPhraseViewData previous = ui2::test::ApprovedPhraseFixture("note");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiPhraseView::Build(previous, deltaPalette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface deltaSurface(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, deltaSurface, deltaPalette);
  deltaSurface.ClearDirty();

  ui2::UiPhraseViewData current = previous;
  current.editRow = 4;
  current.editColumn = 2;
  current.activeHeader = ui2::UiPhraseHeader::Fx1;
  current.rows[7][3] = "BEEF";
  current.playbackRows[1] = 7;
  current.playbackRows[4] = 12;
  current.mutedTracks[4] = true;
  current.cursorBottom = ui2::test::ApprovedPhraseFixture("fx").cursorBottom;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {75, 76, 20, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiPhraseView::Build(current, deltaPalette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiPhraseView::RenderDelta(previous, current, currentScene, deltaSurface,
                                 deltaPalette);

  ui2::UiPalette fullPalette;
  ui2::UiFrameScene fullScene;
  REQUIRE(ui2::UiPhraseView::Build(current, fullPalette, fullScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage fullStorage;
  ui2::UiIndexedSurface fullSurface(fullStorage);
  ui2::UiFrameRenderer::RenderStatic(fullScene, fullSurface, fullPalette);

  CHECK(std::equal(deltaSurface.Pixels().begin(), deltaSurface.Pixels().end(),
                   fullSurface.Pixels().begin(), fullSurface.Pixels().end()));
  CHECK(deltaSurface.DirtyTiles().Any());
}

TEST_CASE("UI2 Phrase bottom bar keeps Note adjustment and FX help distinct") {
  ui2::UiPalette palette;

  ui2::UiPhraseViewData note = ui2::test::ApprovedPhraseFixture("note");
  note.adjustmentFocus = true;
  note.activeHeader = ui2::UiPhraseHeader::Note;
  ui2::UiFrameScene noteScene;
  REQUIRE(ui2::UiPhraseView::Build(note, palette, noteScene) ==
          ui2::UiBuildStatus::Built);
  const auto noteText = noteScene.bottom.Stream().text;
  constexpr std::array<char, 4> noteLabel{'N', 'O', 'T', 'E'};
  constexpr std::array<char, 3> octaveLabel{'O', 'C', 'T'};
  CHECK(std::search(noteText.begin(), noteText.end(), noteLabel.begin(),
                    noteLabel.end()) != noteText.end());
  CHECK(std::search(noteText.begin(), noteText.end(), octaveLabel.begin(),
                    octaveLabel.end()) != noteText.end());

  ui2::UiPhraseViewData fx = ui2::test::ApprovedPhraseFixture("fx");
  fx.adjustmentFocus = true;
  fx.activeHeader = ui2::UiPhraseHeader::Fx1;
  ui2::UiFrameScene fxScene;
  REQUIRE(ui2::UiPhraseView::Build(fx, palette, fxScene) ==
          ui2::UiBuildStatus::Built);
  const auto fxText = fxScene.bottom.Stream().text;
  constexpr std::array<char, 4> killLabel{'K', 'I', 'L', 'L'};
  CHECK(std::search(fxText.begin(), fxText.end(), killLabel.begin(),
                    killLabel.end()) != fxText.end());
  CHECK(std::search(fxText.begin(), fxText.end(), noteLabel.begin(),
                    noteLabel.end()) == fxText.end());

  ui2::UiPhraseViewData empty = ui2::test::ApprovedPhraseFixture("empty");
  ui2::UiFrameScene emptyScene;
  REQUIRE(ui2::UiPhraseView::Build(empty, palette, emptyScene) ==
          ui2::UiBuildStatus::Built);
  CHECK_FALSE(emptyScene.bottomVisible);
}

TEST_CASE("UI2 Phrase dual cursor animation renders exact visual overrides") {
  ui2::UiPalette palette;
  ui2::UiPhraseViewData data = ui2::test::ApprovedPhraseFixture("number");
  data.topMetaVisualOverride = true;
  data.topMetaVisualRect = {75, 9, 15, 9};
  data.topMetaInkVisible = false;
  data.bottomTrackVisualOverride = true;
  data.bottomTrackVisualRect = {83, 211, 15, 9};
  data.bottomTrackInkVisible = false;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiPhraseView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  CHECK(surface.Pixel(82, 13) ==
        palette.Index(ui2::UiColorToken::CursorPrimary));
  CHECK(surface.Pixel(90, 215) ==
        palette.Index(ui2::UiColorToken::CursorPrimary));
  CHECK(ui2::UiChromeRenderer::MetaTargetRect(
            {.title = "PHRASE", .meta = "3A", .metaX = 85}) ==
        ui2::RectI16{83, 9, 15, 9});
  CHECK(ui2::UiChromeRenderer::BottomTrackTargetRect(2) ==
        ui2::RectI16{68, 212, 15, 8});
}

TEST_CASE("UI2 Phrase idle is clean and a cursor move stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiPhraseViewData previous = ui2::test::ApprovedPhraseFixture("note");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiPhraseView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiPhraseView::RenderDelta(previous, previous, previousScene, surface,
                                 palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiPhraseViewData current = previous;
  current.editRow = 11;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiPhraseView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiPhraseView::RenderDelta(previous, current, currentScene, surface,
                                 palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
  CHECK(transferredPixels < 240U * 240U / 7U);
}

TEST_CASE("UI2 Phrase animated bottom cursor delta matches a full frame") {
  ui2::UiPalette palette;
  ui2::UiPhraseViewData previous = ui2::test::ApprovedPhraseFixture("number");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiPhraseView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiPhraseViewData current = previous;
  current.selectedTrack = 4;
  current.bottomTrackVisualOverride = true;
  current.bottomTrackVisualRect = {92, 211, 15, 9};
  current.bottomTrackInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiPhraseView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiPhraseView::RenderDelta(previous, current, currentScene, surface,
                                 palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Table delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiTableViewData previous = ui2::test::ApprovedTableFixture("phrase");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiTableView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiTableViewData current = previous;
  current.editRow = 3;
  current.editColumn = 4;
  current.activeHeader = ui2::UiTableHeader::Fx3;
  current.rows[3][4] = "ARP";
  current.rows[3][5] = "00F4";
  current.playbackRows[0] = 2;
  current.automationPlaybackRows[0] = 8;
  current.automationPlaybackRows[2] = 6;
  current.selectedTrackMuted = true;
  current.cursorBottom =
      ui2::test::ApprovedTableFixture("instrument").cursorBottom;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {101, 62, 21, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiTableView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiTableView::RenderDelta(previous, current, currentScene, surface,
                                palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
  CHECK(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 Table ENTER-held cells retain command help without legend") {
  ui2::UiPalette palette;
  ui2::UiTableViewData data = ui2::test::ApprovedTableFixture("phrase");
  data.adjustmentFocus = true;
  data.enterDigitFocus = false;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiTableView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  const auto text = scene.bottom.Stream().text;
  constexpr std::array<char, 4> killLabel{'K', 'I', 'L', 'L'};
  constexpr std::array<char, 3> octaveLabel{'O', 'C', 'T'};
  CHECK(std::search(text.begin(), text.end(), killLabel.begin(),
                    killLabel.end()) != text.end());
  CHECK(std::search(text.begin(), text.end(), octaveLabel.begin(),
                    octaveLabel.end()) == text.end());

  data.cursorBottom.kind = ui2::UiBottomBarKind::Hidden;
  ui2::UiFrameScene emptyScene;
  REQUIRE(ui2::UiTableView::Build(data, palette, emptyScene) ==
          ui2::UiBuildStatus::Built);
  CHECK_FALSE(emptyScene.bottomVisible);
}

TEST_CASE("UI2 Table idle is clean and row motion stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiTableViewData previous = ui2::test::ApprovedTableFixture("phrase");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiTableView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();
  ui2::UiTableView::RenderDelta(previous, previous, previousScene, surface,
                                palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiTableViewData current = previous;
  current.editRow = 1;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiTableView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiTableView::RenderDelta(previous, current, currentScene, surface,
                                palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
  CHECK(transferredPixels < 240U * 240U / 7U);
}

TEST_CASE(
    "UI2 Instrument delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiInstrumentViewData previous =
      ui2::test::ApprovedInstrumentFixture("sample");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiInstrumentView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiInstrumentViewData current = previous;
  current.name = "BASS 01";
  current.fields[2].value = "F1";
  current.cursor = ui2::UiInstrumentCursor::Name;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 47, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiInstrumentView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiInstrumentView::RenderDelta(previous, current, currentScene, surface,
                                     palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
  CHECK(surface.DirtyTiles().Any());
}
TEST_CASE(
    "UI2 Instrument idle is clean and cursor motion stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiInstrumentViewData previous =
      ui2::test::ApprovedInstrumentFixture("sample");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiInstrumentView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiInstrumentView::RenderDelta(previous, previous, previousScene, surface,
                                     palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiInstrumentViewData current = previous;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 47, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiInstrumentView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiInstrumentView::RenderDelta(previous, current, currentScene, surface,
                                     palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
  CHECK(transferredPixels < 240U * 240U / 7U);
}

TEST_CASE("UI2 Instrument enter mode resolves both independent cursors") {
  ui2::UiInstrumentViewData data =
      ui2::test::ApprovedInstrumentFixture("number");
  data.topMetaVisualOverride = true;
  data.topMetaVisualRect = {57, 9, 15, 9};
  data.topMetaInkVisible = false;
  data.bottomTrackVisualOverride = true;
  data.bottomTrackVisualRect = {84, 211, 15, 9};
  data.bottomTrackInkVisible = false;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiInstrumentView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  CHECK(surface.Pixel(64, 13) ==
        palette.Index(ui2::UiColorToken::CursorPrimary));
  CHECK(surface.Pixel(91, 215) ==
        palette.Index(ui2::UiColorToken::CursorPrimary));
}

TEST_CASE("UI2 Instrument exposes fixed cursor targets for fields and OPAL "
          "operators") {
  ui2::UiInstrumentViewData sample =
      ui2::test::ApprovedInstrumentFixture("sample");
  sample.cursor = ui2::UiInstrumentCursor::Field;
  sample.selectedField = 3;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(sample) ==
        ui2::RectI16{7, 95, 226, 9});

  ui2::UiInstrumentViewData opal = ui2::test::ApprovedInstrumentFixture("opal");
  opal.selectedOperator = 2;
  opal.cursor = ui2::UiInstrumentCursor::Operator1;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(opal) ==
        ui2::RectI16{139, 161, 40, 9});
  opal.cursor = ui2::UiInstrumentCursor::Operator2;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(opal) ==
        ui2::RectI16{185, 161, 40, 9});

  opal.enterSubfieldFocus = true;
  opal.selectedSubfield = 2;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(opal) ==
        ui2::RectI16{200, 161, 9, 9});
}

TEST_CASE("UI2 Instrument warning operator headers and approved adjustment "
          "legend stay semantic") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;

  ui2::UiInstrumentViewData sid =
      ui2::test::ApprovedInstrumentFixture("sid");
  REQUIRE(ui2::UiInstrumentView::Build(sid, palette, scene) ==
          ui2::UiBuildStatus::Built);
  const ui2::UiCommand *experimental =
      FindTextCommand(scene.top.Stream(), "EXPERIMENTAL");
  REQUIRE(experimental != nullptr);
  CHECK(experimental->color ==
        palette.Index(ui2::UiColorToken::SystemWarning));

  ui2::UiInstrumentViewData op1 =
      ui2::test::ApprovedInstrumentFixture("opal");
  op1.cursor = ui2::UiInstrumentCursor::Operator1;
  op1.selectedOperator = 0U;
  REQUIRE(ui2::UiInstrumentView::Build(op1, palette, scene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(FindTextCommand(scene.content.Stream(), "OP 1") != nullptr);
  REQUIRE(FindTextCommand(scene.content.Stream(), "OP 2") != nullptr);
  CHECK(FindTextCommand(scene.content.Stream(), "OP 1")->color ==
        palette.Index(ui2::UiColorToken::TextColored));
  CHECK(FindTextCommand(scene.content.Stream(), "OP 2")->color ==
        palette.Index(ui2::UiColorToken::TextDim));

  ui2::UiInstrumentViewData op2 = op1;
  op2.cursor = ui2::UiInstrumentCursor::Operator2;
  REQUIRE(ui2::UiInstrumentView::Build(op2, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.content.Stream(), "OP 1")->color ==
        palette.Index(ui2::UiColorToken::TextDim));
  CHECK(FindTextCommand(scene.content.Stream(), "OP 2")->color ==
        palette.Index(ui2::UiColorToken::TextColored));
  CheckDeltaMatchesFullFrame(op1, op2, ui2::UiInstrumentView::Build,
                             ui2::UiInstrumentView::RenderDelta);

  ui2::UiInstrumentViewData numeric =
      ui2::test::ApprovedInstrumentFixture("sample");
  numeric.cursor = ui2::UiInstrumentCursor::Field;
  numeric.selectedField = 2U;
  numeric.adjustmentFocus = true;
  numeric.adjustmentFineStep = 1U;
  numeric.adjustmentCoarseStep = 10U;
  REQUIRE(ui2::UiInstrumentView::Build(numeric, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  CHECK(FindTextCommand(scene.bottom.Stream(), "1") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "10") != nullptr);
  ui2::UiInstrumentViewData numericHidden = numeric;
  numericHidden.adjustmentFocus = false;
  CheckDeltaMatchesFullFrame(numericHidden, numeric,
                             ui2::UiInstrumentView::Build,
                             ui2::UiInstrumentView::RenderDelta);

  ui2::UiInstrumentViewData note = numeric;
  note.selectedField = 4U;
  note.adjustmentNote = true;
  note.adjustmentCoarseStep = 12U;
  REQUIRE(ui2::UiInstrumentView::Build(note, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.bottom.Stream(), "NOTE") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "OCT") != nullptr);
}

TEST_CASE("UI2 Instrument reveals the fixed-capacity Sample tail") {
  ui2::UiInstrumentViewData sample =
      ui2::test::ApprovedInstrumentFixture("sample");
  sample.fieldCount = 17U;
  sample.fields[11] = {"INTERPOLATION", "LINEAR", 176};
  sample.fields[12] = {"START", "0000000", 186};
  sample.fields[13] = {"LOOP START", "0000000", 196};
  sample.fields[14] = {"LOOP END", "0000258", 206};
  sample.fields[15] = {"TABLE", "--", 216};
  sample.fields[16] = {"AUTOMATION", "FALSE", 226};
  sample.fields[17] = {"FILTER TYPE", "00", 236};
  sample.fields[18] = {"FILTER MODE", "ORIGINAL", 246};
  sample.fields[19] = {"RESERVED", "--", 256};
  sample.fieldCount = 20U;
  sample.cursor = ui2::UiInstrumentCursor::Field;
  sample.selectedField = 19U;
  sample.fieldBottom = ui2::UiInstrumentFieldBottom::Selector;
  sample.fieldOptions = ui2::UiInstrumentFieldOptions::Boolean;
  sample.scrollOffset = ui2::UiInstrumentView::RevealCursor(0, sample);
  CHECK(sample.scrollOffset == 56);
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiInstrumentView::Build(sample, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.contentOffsetY == 56);
}

TEST_CASE("UI2 Sample Instrument supplies contextual bars for every field kind") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiInstrumentViewData data =
      ui2::test::ApprovedInstrumentFixture("sample");
  data.cursor = ui2::UiInstrumentCursor::Field;

  data.fieldBottom = ui2::UiInstrumentFieldBottom::Open;
  REQUIRE(ui2::UiInstrumentView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.bottom.Stream(), "OPEN") != nullptr);

  data.fieldBottom = ui2::UiInstrumentFieldBottom::Adjustment;
  data.adjustmentFineStep = 1U;
  data.adjustmentCoarseStep = 16U;
  REQUIRE(ui2::UiInstrumentView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.bottom.Stream(), "1") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "16") != nullptr);

  data.fieldBottom = ui2::UiInstrumentFieldBottom::Selector;
  data.fieldOptions = ui2::UiInstrumentFieldOptions::SampleLoop;
  data.fieldOptionWrap = true;
  data.fieldOptionCurrent = 2U;
  REQUIRE(ui2::UiInstrumentView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.bottom.Stream(), "PING PONG") != nullptr);

  data.fieldOptions = ui2::UiInstrumentFieldOptions::Boolean;
  data.fieldOptionCurrent = 1U;
  REQUIRE(ui2::UiInstrumentView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.bottom.Stream(), "YES") != nullptr);
}

TEST_CASE("UI2 Sample Instrument preserves sample filename case while unfocused") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiInstrumentViewData data;
  data.kind = ui2::UiInstrumentKind::Sample;
  data.cursor = ui2::UiInstrumentCursor::Type;
  data.fields[0] = {"SAMPLE", "Kick_One.wav", 66, true};
  data.fieldCount = 1U;

  REQUIRE(ui2::UiInstrumentView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindLiteralTextCommand(scene.content.Stream(), "Kick_One.wav") !=
        nullptr);
}

TEST_CASE("UI2 Sample Editor preserves sample filename case while unfocused") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiSampleEditorViewData data;
  data.name = "Kick_One.wav";
  data.cursor = ui2::UiSampleEditorCursor::Start;

  REQUIRE(ui2::UiSampleEditorView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindLiteralTextCommand(scene.content.Stream(), "Kick_One.wav") !=
        nullptr);
}

TEST_CASE("UI2 message dialogs preserve user-authored label case") {
  Ui2DialogSnapshot snapshot;
  snapshot.kind = ui2::UiDialogKind::Message;
  snapshot.SetTitle("Remove sample?");
  snapshot.SetUserLabel("Kick_One.wav");
  snapshot.PushAction(ui2::UiDialogAction::No);

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(snapshot.ToViewData(), scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindLiteralTextCommand(scene.overlay.Stream(), "Kick_One.wav") !=
        nullptr);
}

TEST_CASE(
    "UI2 Instrument field focus delta is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiInstrumentViewData previous =
      ui2::test::ApprovedInstrumentFixture("sample");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiInstrumentView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiInstrumentViewData current = previous;
  current.cursor = ui2::UiInstrumentCursor::Field;
  current.selectedField = 4;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiInstrumentView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiInstrumentView::RenderDelta(previous, current, currentScene, surface,
                                     palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Mixer stereo meters use separate damage columns") {
  CHECK(ui2::UiMixerView::MeterDamageRect(0, 0) == ui2::RectI16{7, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterDamageRect(0, 1) ==
        ui2::RectI16{17, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterDamageRect(8, 0) ==
        ui2::RectI16{209, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterDamageRect(8, 1) ==
        ui2::RectI16{219, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterLevelDamageRect(0, 0, 30, 37) ==
        ui2::RectI16{7, 76, 7, 7});
  CHECK(ui2::UiMixerView::MeterLevelDamageRect(0, 1, 37, 30) ==
        ui2::RectI16{17, 76, 7, 7});
  CHECK(ui2::UiMixerView::MeterLevelDamageRect(0, 0, 37, 37).Empty());
  CHECK(ui2::UiMixerView::MeterLevelDamageRect(8, 1, 200, 152) ==
        ui2::RectI16{219, 198, 7, 1});
}

TEST_CASE("UI2 Mixer delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiMixerViewData previous = ui2::test::ApprovedMixerFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiMixerView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiMixerViewData current = previous;
  current.vuLevelTop[3] = {71, 83};
  current.volumes[3] = "7F";
  current.selectedChannel = 3;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiMixerView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiMixerView::RenderDelta(previous, current, currentScene, surface,
                                palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
  CHECK(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 Mixer idle is clean and one meter change stays local") {
  ui2::UiPalette palette;
  ui2::UiMixerViewData previous = ui2::test::ApprovedMixerFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiMixerView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiMixerView::RenderDelta(previous, previous, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiMixerViewData current = previous;
  current.vuLevelTop[6][1] = 91;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiMixerView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiMixerView::RenderDelta(previous, current, currentScene, surface,
                                palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 3'000);
}

TEST_CASE("UI2 Mixer all stereo meters can advance within one frame budget") {
  ui2::UiPalette palette;
  ui2::UiMixerViewData previous = ui2::test::ApprovedMixerFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiMixerView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiMixerViewData current = previous;
  for (std::uint8_t channel = 0; channel < ui2::UiMixerView::kChannelCount;
       ++channel) {
    for (std::uint8_t side = 0; side < 2U; ++side) {
      const std::uint8_t previousTop = previous.vuLevelTop[channel][side];
      current.vuLevelTop[channel][side] =
          previousTop < ui2::UiMixerView::kMeterHeight
              ? static_cast<std::uint8_t>(previousTop + 1U)
              : static_cast<std::uint8_t>(previousTop - 1U);
    }
  }
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiMixerView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiMixerView::RenderDelta(previous, current, currentScene, surface,
                                palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));

  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels <= 4'608);
  CHECK(transferredPixels <= 240U * 240U / 12U);
}

TEST_CASE("UI2 Groove delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiGrooveViewData previous = ui2::test::ApprovedGrooveFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiGrooveView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiGrooveViewData current = previous;
  current.editRow = 7;
  current.steps[7] = 0x09;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {27, 86, 15, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiGrooveView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiGrooveView::RenderDelta(previous, current, currentScene, surface,
                                 palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Groove idle is clean and a row move stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiGrooveViewData previous = ui2::test::ApprovedGrooveFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiGrooveView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiGrooveView::RenderDelta(previous, previous, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiGrooveViewData current = previous;
  current.editRow = 1;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiGrooveView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiGrooveView::RenderDelta(previous, current, currentScene, surface,
                                 palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 3'000);
}

TEST_CASE("UI2 Chain keeps stereo VU channels physically separate") {
  CHECK(ui2::UiChainView::RowDamageRect(0) == ui2::RectI16{5, 46, 213, 12});
  CHECK(ui2::UiChainView::VuDamageRect(0) == ui2::RectI16{219, 47, 7, 153});
  CHECK(ui2::UiChainView::VuDamageRect(1) == ui2::RectI16{228, 47, 7, 153});
}

TEST_CASE("UI2 Chain transpose uses signed three-glyph decimal semantics") {
  CHECK(ui2::Ui2ChainTranspose::Format(0U) ==
        std::array<char, 4>{'+', '0', '0', 0});
  CHECK(ui2::Ui2ChainTranspose::Format(ui2::Ui2ChainTranspose::Encode(-12)) ==
        std::array<char, 4>{'-', '1', '2', 0});
  CHECK(ui2::Ui2ChainTranspose::Format(0xFFU) ==
        std::array<char, 4>{'-', '0', '1', 0});
  CHECK(ui2::Ui2ChainTranspose::Format(ui2::Ui2ChainTranspose::Encode(7)) ==
        std::array<char, 4>{'+', '0', '7', 0});
  CHECK(ui2::Ui2ChainTranspose::Decode(
            ui2::Ui2ChainTranspose::Adjust(0U, -12)) == -12);
  CHECK(ui2::Ui2ChainTranspose::Decode(ui2::Ui2ChainTranspose::Adjust(
            ui2::Ui2ChainTranspose::Encode(-95), -12)) == -99);
  CHECK(ui2::Ui2ChainTranspose::Decode(ui2::Ui2ChainTranspose::Adjust(
            ui2::Ui2ChainTranspose::Encode(95), 12)) == 99);

  ui2::UiChainViewData data = ui2::test::ApprovedChainFixture();
  data.transposes[0] = ui2::Ui2ChainTranspose::Encode(-12);
  data.transposes[1] = ui2::Ui2ChainTranspose::Encode(7);
  data.transposes[2] = 0U;
  data.editColumn = 1U;
  data.adjustmentFocus = true;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiChainView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.content.Stream(), "-12") != nullptr);
  CHECK(FindTextCommand(scene.content.Stream(), "+07") != nullptr);
  CHECK(FindTextCommand(scene.content.Stream(), "---") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "OCT") != nullptr);
  CHECK(ui2::UiChainView::CursorTargetRect(data) ==
        ui2::RectI16{49, 47, 21, 9});

  bool coarsePlusMinusPresent = false;
  for (const ui2::UiCommand &command : scene.bottom.Commands()) {
    if (command.kind == ui2::UiCommandKind::FillRect &&
        command.bounds.x >= 170 && command.bounds.x < 175) {
      coarsePlusMinusPresent = true;
    }
  }
  CHECK_FALSE(coarsePlusMinusPresent);
}

TEST_CASE("UI2 Chain delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiChainViewData previous = ui2::test::ApprovedChainFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiChainView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiChainViewData current = previous;
  current.editRow = 6;
  current.phrases[6] = 0x4C;
  current.playbackRows[1] = 2;
  current.playbackRows[4] = 11;
  current.mutedTracks[4] = true;
  current.trackNotes[4] = "C#4";
  current.vuLevelTop[1] = 80;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {31, 76, 15, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiChainView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiChainView::RenderDelta(previous, current, currentScene, surface,
                                palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Chain idle is clean and a cursor move stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiChainViewData previous = ui2::test::ApprovedChainFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiChainView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiChainView::RenderDelta(previous, previous, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiChainViewData current = previous;
  current.editRow = 1;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiChainView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiChainView::RenderDelta(previous, current, currentScene, surface,
                                palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
}

TEST_CASE("UI2 Project resolves cursor-specific bottom bars") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiProjectView::Build(ui2::test::ApprovedProjectFixture("name"),
                                    palette,
                                    scene) == ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  const ui2::UiCommand *newAction =
      FindTextCommand(scene.bottom.Stream(), "NEW");
  const ui2::UiCommand *loadAction =
      FindTextCommand(scene.bottom.Stream(), "LOAD");
  const ui2::UiCommand *saveAction =
      FindTextCommand(scene.bottom.Stream(), "SAVE");
  const ui2::UiCommand *renameAction =
      FindTextCommand(scene.bottom.Stream(), "RENAME");
  REQUIRE(newAction != nullptr);
  REQUIRE(loadAction != nullptr);
  REQUIRE(saveAction != nullptr);
  REQUIRE(renameAction != nullptr);
  CHECK(newAction->bounds.x < loadAction->bounds.x);
  CHECK(loadAction->bounds.x < saveAction->bounds.x);
  CHECK(saveAction->bounds.x < renameAction->bounds.x);
  REQUIRE(
      ui2::UiProjectView::Build(ui2::test::ApprovedProjectFixture("playback"),
                                palette, scene) == ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  REQUIRE(
      ui2::UiProjectView::Build(ui2::test::ApprovedProjectFixture("cleanup"),
                                palette, scene) == ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  REQUIRE(ui2::UiProjectView::Build(ui2::test::ApprovedProjectFixture("render"),
                                    palette,
                                    scene) == ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
}

TEST_CASE("UI2 Project exposes every approved conceptual row and action bar") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiProjectViewData data;
  constexpr std::array playback{
      ui2::UiProjectCursor::Tempo, ui2::UiProjectCursor::Transpose,
      ui2::UiProjectCursor::Scale, ui2::UiProjectCursor::Root};
  for (const auto cursor : playback) {
    data.cursor = cursor;
    CHECK_FALSE(ui2::UiProjectView::CursorTargetRect(cursor).Empty());
    REQUIRE(ui2::UiProjectView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    CHECK(scene.bottomVisible);
  }
  constexpr std::array contextual{
      ui2::UiProjectCursor::Name, ui2::UiProjectCursor::SamplePool,
      ui2::UiProjectCursor::Samples, ui2::UiProjectCursor::Instruments,
      ui2::UiProjectCursor::Render};
  for (const auto cursor : contextual) {
    data.cursor = cursor;
    CHECK_FALSE(ui2::UiProjectView::CursorTargetRect(cursor).Empty());
    REQUIRE(ui2::UiProjectView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    CHECK(scene.bottomVisible);
  }
}

TEST_CASE("UI2 Project delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiProjectViewData previous = ui2::test::ApprovedProjectFixture("name");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiProjectView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiProjectViewData current = ui2::test::ApprovedProjectFixture("render");
  current.name = "LIVE SET";
  current.tempo = "140";
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiProjectView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiProjectView::RenderDelta(previous, current, currentScene, surface,
                                  palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Project idle is clean and animated cursor damage stays local") {
  ui2::UiPalette palette;
  ui2::UiProjectViewData previous =
      ui2::test::ApprovedProjectFixture("playback");
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiProjectView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiProjectView::RenderDelta(previous, previous, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiProjectViewData current = previous;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 75, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiProjectView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiProjectView::RenderDelta(previous, current, currentScene, surface,
                                  palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 240U * 240U / 4U);
}

TEST_CASE("UI2 Device delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiDeviceViewData previous = ui2::test::ApprovedDeviceFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDeviceView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiDeviceViewData current = previous;
  current.midiDevice = "ON";
  current.volume = "55";
  current.theme = "NIGHT";
  current.batteryPercent = 82;
  current.batteryPercentValid = false;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 59, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiDeviceView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiDeviceView::RenderDelta(previous, current, currentScene, surface,
                                 palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Device idle frame stays clean") {
  ui2::UiPalette palette;
  ui2::UiDeviceViewData data = ui2::test::ApprovedDeviceFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDeviceView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiDeviceView::RenderDelta(data, data, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 Device omits an unavailable battery percentage") {
  ui2::UiPalette palette;
  ui2::UiDeviceViewData data = ui2::test::ApprovedDeviceFixture();
  data.batteryPercent = 0;
  data.batteryPercentValid = false;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDeviceView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  const ui2::PaletteIndex topBackground =
      palette.Index(ui2::UiColorToken::SurfaceTopBar);
  for (std::int16_t y = 14; y < 21; ++y) {
    for (std::int16_t x = 184; x < 207; ++x)
      CHECK(surface.Pixel(x, y) == topBackground);
  }
}

TEST_CASE("UI2 Device represents all approved rows and reveals optional rows") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiDeviceViewData data;
  data.showLineOut = true;
  data.showUpdateFirmware = true;
  data.selectorOptions = {"OFF", "TRS", "USB", "TRS+USB"};
  data.selectorCount = 4;
  data.selectorCurrent = 2;
  constexpr std::array cursors{
      ui2::UiDeviceCursor::MidiDevice, ui2::UiDeviceCursor::MidiSync,
      ui2::UiDeviceCursor::LineOut,    ui2::UiDeviceCursor::Resampler,
      ui2::UiDeviceCursor::Volume,
      ui2::UiDeviceCursor::Brightness, ui2::UiDeviceCursor::Theme,
      ui2::UiDeviceCursor::Font,       ui2::UiDeviceCursor::UpdateFirmware};
  for (const auto cursor : cursors) {
    data.cursor = cursor;
    CHECK_FALSE(ui2::UiDeviceView::CursorTargetRect(data).Empty());
    REQUIRE(ui2::UiDeviceView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
  }
  data.cursor = ui2::UiDeviceCursor::UpdateFirmware;
  data.scrollOffset = ui2::UiDeviceView::RevealCursor(0, data);
  CHECK(data.scrollOffset > 0);
  REQUIRE(ui2::UiDeviceView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.contentOffsetY == data.scrollOffset);
}

TEST_CASE("UI2 Theme and Font remain separate page contracts") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiThemeViewData theme;
  theme.nameAction = 3;
  REQUIRE(ui2::UiThemeView::Build(theme, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);

  theme.selectedColor = 0;
  theme.selectedRgb = {3U, 7U, 255U};
  theme.colorComponent = 1U;
  REQUIRE(ui2::UiThemeView::Build(theme, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  CHECK(FindTextCommand(scene.bottom.Stream(), "3") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "7") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "255") != nullptr);
  CHECK((ui2::UiChromeRenderer::BottomRgbTargetRect(0U, 0U) ==
         ui2::RectI16{45, 218, 9, 11}));
  CHECK((ui2::UiChromeRenderer::BottomRgbTargetRect(1U, 128U) ==
         ui2::RectI16{117, 218, 21, 11}));
  CHECK((ui2::UiChromeRenderer::BottomRgbTargetRect(2U, 255U) ==
         ui2::RectI16{195, 218, 21, 11}));
  CHECK(ui2::UiChromeRenderer::BottomRgbTargetRect(3U, 0U).Empty());

  ui2::UiThemeViewState retained;
  retained.selectedColor = 18;
  retained.selectedRgb = {0U, 127U, 255U};
  retained.colorComponent = 2U;
  const ui2::UiThemeViewData projected = retained.ToViewData();
  CHECK(projected.selectedColor == 18);
  CHECK(projected.selectedRgb == retained.selectedRgb);
  CHECK(projected.colorComponent == 2U);

  REQUIRE(ui2::UiFontView::Build({}, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
}

TEST_CASE("UI2 Font case choices remain literal under every case mode") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiFontViewData data;
  data.textCase = "CASE";
  REQUIRE(ui2::UiFontView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);

  CHECK(FindLiteralTextCommand(scene.content.Stream(), "CASE") != nullptr);
  CHECK(FindLiteralTextCommand(scene.bottom.Stream(), "BROWSE") != nullptr);
  CHECK(FindLiteralTextCommand(scene.bottom.Stream(), "DEFAULT") != nullptr);

  ui2::UiSurfaceStorage upperStorage;
  ui2::UiIndexedSurface upper(upperStorage);
  scene.textCase = ui2::UiTextCaseMode::Upper;
  ui2::UiFrameRenderer::RenderStatic(scene, upper, palette);

  ui2::UiSurfaceStorage lowerStorage;
  ui2::UiIndexedSurface lower(lowerStorage);
  scene.textCase = ui2::UiTextCaseMode::Lower;
  ui2::UiFrameRenderer::RenderStatic(scene, lower, palette);

  bool pageLabelsChanged = false;
  for (std::int16_t y = 0; y < 208; ++y) {
    for (std::int16_t x = 0; x < 240; ++x) {
      pageLabelsChanged = pageLabelsChanged ||
                          upper.Pixel(x, y) != lower.Pixel(x, y);
    }
  }
  CHECK(pageLabelsChanged);
  for (std::int16_t y = 208; y < 240; ++y) {
    for (std::int16_t x = 0; x < 240; ++x)
      CHECK(upper.Pixel(x, y) == lower.Pixel(x, y));
  }
}

TEST_CASE("UI2 user-selected theme and font names preserve authored case") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;

  ui2::UiDeviceViewData device;
  device.theme = "NightShift";
  device.font = "SoftFace.npf";
  device.showTheme = true;
  device.showFont = true;
  REQUIRE(ui2::UiDeviceView::Build(device, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindLiteralTextCommand(scene.content.Stream(), "NightShift") !=
        nullptr);
  CHECK(FindLiteralTextCommand(scene.content.Stream(), "SoftFace.npf") !=
        nullptr);

  ui2::UiFontViewData font;
  font.font = "SoftFace.npf";
  REQUIRE(ui2::UiFontView::Build(font, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindLiteralTextCommand(scene.content.Stream(), "SoftFace.npf") !=
        nullptr);
}

TEST_CASE("UI2 Font bottom bar exposes only BROWSE and DEFAULT") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiFontViewData data;
  data.cursor = ui2::UiFontCursor::Browse;
  data.feedback = "FONT BROWSER UNAVAILABLE";
  REQUIRE(ui2::UiFontView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);

  REQUIRE(scene.bottomVisible);
  CHECK(FindTextCommand(scene.bottom.Stream(), "BROWSE") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "DEFAULT") != nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "Case") == nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "CASE") == nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "case") == nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "Regular") == nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "Bold") == nullptr);
  CHECK(FindTextCommand(scene.bottom.Stream(), "Wide") == nullptr);
  CHECK(FindTextCommand(scene.content.Stream(), "FONT BROWSER UNAVAILABLE") !=
        nullptr);
}

TEST_CASE("UI2 Theme and Font adapters retain owned fixed-capacity text") {
  ThemeViewUi2Snapshot themeSnapshot;
  constexpr std::array themeName{'N', 'I', 'G', 'H', 'T', '\0'};
  std::copy(themeName.begin(), themeName.end(), themeSnapshot.name.begin());
  themeSnapshot.focus = ThemeViewUi2Focus::Color;
  themeSnapshot.selectedColor = 11;
  themeSnapshot.nameAction = 2;
  themeSnapshot.colorsValid = true;
  themeSnapshot.colors[11] = 0x123456U;

  const ui2::UiThemeViewState themeState =
      ui2::MakeUiThemeViewState(themeSnapshot, ui2::UiPowerState::BatteryHigh);
  themeSnapshot.name[0] = 'X';
  const ui2::UiThemeViewData themeData = themeState.ToViewData();
  CHECK(themeData.name == "NIGHT");
  CHECK(themeData.selectedColor == 11);
  const std::array<std::uint8_t, 3> expectedRgb{0x12U, 0x34U, 0x56U};
  CHECK(themeData.selectedRgb == expectedRgb);
  CHECK(themeData.nameAction == 2);
  CHECK(themeData.power == ui2::UiPowerState::BatteryHigh);

  FontViewUi2Snapshot fontSnapshot;
  constexpr std::array fontName{'W', 'I', 'D', 'E', '\0'};
  std::copy(fontName.begin(), fontName.end(), fontSnapshot.font.begin());
  const ui2::UiFontViewState fontState =
      ui2::MakeUiFontViewState(fontSnapshot, ui2::UiPowerState::Charging);
  fontSnapshot.font[0] = 'X';
  const ui2::UiFontViewData fontData = fontState.ToViewData();
  CHECK(fontData.font == "WIDE");
  CHECK(fontData.power == ui2::UiPowerState::Charging);
}

TEST_CASE("UI2 settings adapters terminate malformed fixed-capacity text") {
  ThemeViewUi2Snapshot themeSnapshot;
  themeSnapshot.name.fill('N');
  themeSnapshot.focus = ThemeViewUi2Focus::Color;
  themeSnapshot.selectedColor = 99;
  themeSnapshot.nameAction = 99;
  const ui2::UiThemeViewState themeState =
      ui2::MakeUiThemeViewState(themeSnapshot);
  CHECK(themeState.ToViewData().name.size() ==
        ThemeViewUi2Snapshot::NameCapacity - 1U);
  CHECK(themeState.ToViewData().selectedColor == -1);
  CHECK(themeState.ToViewData().nameAction == 0);

  FontViewUi2Snapshot fontSnapshot;
  fontSnapshot.font.fill('F');
  const ui2::UiFontViewState fontState = ui2::MakeUiFontViewState(fontSnapshot);
  CHECK(fontState.ToViewData().font.size() ==
        FontViewUi2Snapshot::FontCapacity - 1U);
}

TEST_CASE("UI2 legacy Theme focus adapter exposes real controller coverage") {
  CHECK(ui2::AdaptLegacyThemeFocus(0).focus == ThemeViewUi2Focus::Name);
  CHECK(ui2::AdaptLegacyThemeFocus(0).nameAction ==
        static_cast<std::uint8_t>(ui2::UiThemeNameAction::Rename));
  CHECK(ui2::AdaptLegacyThemeFocus(1).nameAction ==
        static_cast<std::uint8_t>(ui2::UiThemeNameAction::Load));
  CHECK(ui2::AdaptLegacyThemeFocus(2).nameAction ==
        static_cast<std::uint8_t>(ui2::UiThemeNameAction::Save));
  CHECK(ui2::AdaptLegacyThemeFocus(3).focus == ThemeViewUi2Focus::Font);
  CHECK(ui2::AdaptLegacyThemeFocus(4).focus == ThemeViewUi2Focus::Unknown);
  for (std::int16_t component = 5; component <= 7; ++component) {
    CHECK(ui2::AdaptLegacyThemeFocus(component).focus ==
          ThemeViewUi2Focus::Color);
    CHECK(ui2::AdaptLegacyThemeFocus(component).selectedColor == 3);
  }
  CHECK(ui2::AdaptLegacyThemeFocus(8).focus == ThemeViewUi2Focus::Unknown);
  // Legacy Highlight1 is not the source of any one independently editable
  // semantic UI2 row; pretending it is text.dim would edit Emphasis instead.
  CHECK(ui2::AdaptLegacyThemeFocus(15).focus == ThemeViewUi2Focus::Unknown);
  CHECK(ui2::AdaptLegacyThemeFocus(60).selectedColor == 4);
  CHECK(ui2::AdaptLegacyThemeFocus(64).focus == ThemeViewUi2Focus::Unknown);

  ThemeViewUi2Snapshot legacy;
  legacy.nameActionMask = ui2::kLegacyThemeNameActionMask;
  legacy.editableColorMask = ui2::kLegacyThemeEditableColorMask;
  CHECK_FALSE(legacy.HasNameAction(
      static_cast<std::uint8_t>(ui2::UiThemeNameAction::New)));
  CHECK(legacy.HasNameAction(
      static_cast<std::uint8_t>(ui2::UiThemeNameAction::Load)));
  CHECK(legacy.IsColorEditable(3));
  CHECK_FALSE(legacy.IsColorEditable(18));
  CHECK_FALSE(ui2::ThemeControllerCoversApprovedContract(legacy));

  legacy.nameActionMask = ui2::kApprovedThemeNameActionMask;
  legacy.editableColorMask = ui2::kApprovedThemeEditableColorMask;
  CHECK(ui2::ThemeControllerCoversApprovedContract(legacy));
}

TEST_CASE("UI2 Theme palette synchronization is explicit and reversible") {
  ThemeViewUi2Snapshot snapshot;
  snapshot.colorsValid = true;
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    snapshot.colors[index] =
        static_cast<std::uint32_t>(0x010203U + index * 0x070B0DU) & 0x00FFFFFFU;
  }

  ui2::UiPalette palette;
  const ui2::Rgb888 before = palette.Get(0);
  const auto state = ui2::MakeUiThemeViewState(snapshot);
  CHECK(state.ToViewData().name == "");
  CHECK(palette.Get(0) == before);

  CHECK(ui2::ApplyThemeSnapshotToPalette(snapshot, palette));
  ui2::UiPalette sequentialReference;
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    const std::uint32_t packed = snapshot.colors[index];
    const ui2::Rgb888 expected{static_cast<std::uint8_t>(packed >> 16U),
                               static_cast<std::uint8_t>(packed >> 8U),
                               static_cast<std::uint8_t>(packed)};
    CHECK(palette.Get(static_cast<ui2::PaletteIndex>(index)) == expected);
    sequentialReference.Set(static_cast<ui2::PaletteIndex>(index), expected);
  }
  for (std::size_t index = 0; index < ui2::UiPalette::kColorCount; ++index)
    CHECK(palette.Get(static_cast<ui2::PaletteIndex>(index)) ==
          sequentialReference.Get(static_cast<ui2::PaletteIndex>(index)));

  ThemeViewUi2Snapshot roundTrip;
  ui2::CopyPaletteToThemeSnapshot(palette, roundTrip);
  CHECK(roundTrip.colorsValid);
  CHECK(roundTrip.colors == snapshot.colors);
}

TEST_CASE("UI2 Theme rejects an invalid compatibility palette snapshot") {
  ThemeViewUi2Snapshot snapshot;
  snapshot.colors.fill(0x00FF00U);
  ui2::UiPalette palette;
  const ui2::Rgb888 before = palette.Get(0);
  CHECK_FALSE(ui2::ApplyThemeSnapshotToPalette(snapshot, palette));
  CHECK(palette.Get(0) == before);
}

TEST_CASE("UI2 Theme bottom action changes use a pixel-identical delta") {
  ui2::UiThemeViewData previous;
  ui2::UiThemeViewData current = previous;
  current.nameAction = 3;
  CheckDeltaMatchesFullFrame(previous, current, ui2::UiThemeView::Build,
                             ui2::UiThemeView::RenderDelta);

  current.selectedColor = 0;
  CheckDeltaMatchesFullFrame(previous, current, ui2::UiThemeView::Build,
                             ui2::UiThemeView::RenderDelta);

  previous = current;
  current.selectedRgb = {255U, 127U, 0U};
  current.colorComponent = 1U;
  CheckDeltaMatchesFullFrame(previous, current, ui2::UiThemeView::Build,
                             ui2::UiThemeView::RenderDelta);
}

TEST_CASE("UI2 Theme delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiThemeViewData previous;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiThemeView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiThemeViewData current = previous;
  current.name = "NIGHT";
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 47, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiThemeView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiThemeView::RenderDelta(previous, current, currentScene, surface,
                                palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Font delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiFontViewData previous;
  const std::string longFont(25U, 'W');
  previous.font = longFont;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiFontView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiFontViewData current = previous;
  current.font = "I";
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 59, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiFontView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiFontView::RenderDelta(previous, current, currentScene, surface,
                               palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Font cursor delta matches every CASE to FONT animation frame") {
  const ui2::RectI16 caseTarget =
      ui2::UiFontView::CursorTargetRect(ui2::UiFontCursor::TextCase);
  const ui2::RectI16 fontTarget =
      ui2::UiFontView::CursorTargetRect(ui2::UiFontCursor::Browse);
  const auto checkDelta = [](const ui2::UiFontViewData &previous,
                             const ui2::UiFontViewData &current) {
    CheckDeltaMatchesFullFrame(previous, current, ui2::UiFontView::Build,
                               ui2::UiFontView::RenderDelta);
  };

  ui2::UiFontViewData previous;
  previous.cursor = ui2::UiFontCursor::TextCase;
  previous.cursorVisualOverride = true;
  previous.cursorVisualRect = caseTarget;
  previous.cursorInkVisible = true;

  ui2::UiFontViewData current = previous;
  current.cursor = ui2::UiFontCursor::Browse;
  current.cursorInkVisible = false;
  checkDelta(previous, current);

  ui2::UiAnimatedRect motion;
  motion.Snap(caseTarget, 0U);
  motion.Retarget(fontTarget, 1U, 120U);
  previous = current;
  current.cursorVisualRect = motion.Sample(61U);
  CHECK(current.cursorVisualRect == ui2::RectI16{7, 80, 226, 9});
  checkDelta(previous, current);

  previous = current;
  current.cursorVisualRect = motion.Sample(121U);
  current.cursorInkVisible = true;
  CHECK(current.cursorVisualRect == fontTarget);
  checkDelta(previous, current);

  previous = current;
  current.cursor = ui2::UiFontCursor::TextCase;
  current.cursorInkVisible = false;
  checkDelta(previous, current);

  motion.Retarget(caseTarget, 122U, 120U);
  previous = current;
  current.cursorVisualRect = motion.Sample(182U);
  CHECK(current.cursorVisualRect.y > caseTarget.y);
  CHECK(current.cursorVisualRect.y < fontTarget.y);
  checkDelta(previous, current);

  previous = current;
  current.cursorVisualRect = motion.Sample(242U);
  current.cursorInkVisible = true;
  CHECK(current.cursorVisualRect == caseTarget);
  checkDelta(previous, current);
}

TEST_CASE("UI2 shared browser delta is pixel-identical across page variants") {
  ui2::UiPalette palette;
  ui2::UiBrowserViewData previous =
      ui2::test::ApprovedBrowserFixture("sample-pool");
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiBrowserView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiBrowserViewData current =
      ui2::test::ApprovedBrowserFixture("projects");
  current.items[0] = "LIVE SET";
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 49, 226, 11};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiBrowserView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiBrowserView::RenderDelta(previous, current, currentScene, surface,
                                  palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 shared browser uses a bounded visible window and scroll thumb") {
  ui2::UiPalette palette;
  ui2::UiBrowserViewData data =
      ui2::test::ApprovedBrowserFixture("instrument-import");
  data.items[0] = "[..]";
  data.items[1] = "BASS.PTI";
  data.items[2] = "LEAD.PTI";
  data.visibleItemCount = 3;
  data.selectedRow = 2;
  data.topIndex = 7;
  data.totalItemCount = 30;

  CHECK(ui2::UiBrowserView::CursorTargetRect(data.selectedRow) ==
        ui2::RectI16{7, 65, 226, 11});
  const ui2::RectI16 thumb = ui2::UiBrowserView::ScrollThumbRect(data);
  CHECK_FALSE(thumb.Empty());
  CHECK(thumb.x == 235);
  CHECK(thumb.y > 43);

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiBrowserView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  CHECK(surface.Pixel(8, 70) ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::CursorPrimary));
  CHECK(surface.Pixel(235, thumb.y + thumb.height / 2) ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextColored));
}

TEST_CASE("UI2 shared browser clamps malformed action metadata") {
  ui2::UiBrowserViewData data = ui2::test::ApprovedBrowserFixture("projects");
  data.actions = {"LOAD", "DELETE", "RENAME"};
  data.actionCount = 0xFFU;
  data.activeAction = 0xFFU;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiBrowserView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.bottom.Overflowed());
  CHECK(scene.bottom.Size() == 3);
}

TEST_CASE("UI2 shared browser redraws a scrolled window pixel-identically") {
  ui2::UiPalette palette;
  ui2::UiBrowserViewData previous =
      ui2::test::ApprovedBrowserFixture("projects");
  previous.items[1] = "LIVE SET";
  previous.items[2] = "NEW JAM";
  previous.visibleItemCount = 3;
  previous.totalItemCount = 20;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiBrowserView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiBrowserViewData current = previous;
  current.items[0] = "LIVE SET";
  current.items[1] = "NEW JAM";
  current.items[2] = "AMBIENT";
  current.selectedRow = 2;
  current.topIndex = 1;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiBrowserView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiBrowserView::RenderDelta(previous, current, currentScene, surface,
                                  palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 shared browser idle frame stays clean") {
  ui2::UiPalette palette;
  ui2::UiBrowserViewData data =
      ui2::test::ApprovedBrowserFixture("theme-import");
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiBrowserView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiBrowserView::RenderDelta(data, data, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 empty browser ignores hidden cursor animation geometry") {
  ui2::UiPalette palette;
  ui2::UiBrowserViewData previous =
      ui2::test::ApprovedBrowserFixture("projects");
  previous.items = {};
  previous.visibleItemCount = 0;
  previous.selectedRow = 0;
  previous.topIndex = 0;
  previous.totalItemCount = 0;
  previous.cursorVisualOverride = false;
  previous.cursorInkVisible = false;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiBrowserView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiBrowserViewData current = previous;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 43, 226, 11};
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiBrowserView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiBrowserView::RenderDelta(previous, current, currentScene, surface,
                                  palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 Record delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiRecordViewData previous;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiRecordView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiRecordViewData current = previous;
  current.source = "MIC";
  current.lineGain = "-3 DB";
  current.micGain = "+6 DB";
  current.elapsed = "01:23";
  current.safeWidth = 128;
  current.warningWidth = 60;
  current.power = ui2::UiPowerState::BatteryLow;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 64, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiRecordView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiRecordView::RenderDelta(previous, current, currentScene, surface,
                                 palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Record idle is clean and animated cursor damage stays local") {
  ui2::UiPalette palette;
  ui2::UiRecordViewData previous;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiRecordView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiRecordView::RenderDelta(previous, previous, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiRecordViewData current = previous;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 53, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiRecordView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiRecordView::RenderDelta(previous, current, currentScene, surface,
                                 palette);
  ui2::DirtyStripList strips;
  REQUIRE(surface.DirtyTiles().Collect(strips));
  std::uint32_t transferredPixels = 0;
  for (const ui2::DirtyStrip strip : strips.Strips()) {
    transferredPixels += static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 240U * 240U / 4U);
}

TEST_CASE("UI2 Record centers semantic state text and saving progress") {
  struct Case {
    ui2::UiRecordState state;
    std::string_view text;
    ui2::UiColorToken color;
    bool bottomVisible;
  };
  constexpr std::array cases{
      Case{ui2::UiRecordState::Armed, "PRESS PLAY TO RECORD",
           ui2::UiColorToken::PlaybackActive, true},
      Case{ui2::UiRecordState::Recording, "PRESS PLAY TO STOP",
           ui2::UiColorToken::SystemError, true},
      Case{ui2::UiRecordState::Saving, "SAVING",
           ui2::UiColorToken::SystemWarning, false},
      Case{ui2::UiRecordState::Unavailable, "RECORDING UNAVAILABLE",
           ui2::UiColorToken::SystemWarning, false},
  };

  for (const Case &test : cases) {
    ui2::UiRecordViewData data;
    data.state = test.state;
    if (test.state == ui2::UiRecordState::Unavailable)
      data.focus = ui2::UiRecordFocus::None;
    data.savingPercent = 42U;
    ui2::UiPalette palette;
    ui2::UiFrameScene scene;
    REQUIRE(ui2::UiRecordView::Build(data, palette, scene) ==
            ui2::UiBuildStatus::Built);
    CHECK(scene.bottomVisible == test.bottomVisible);
    if (test.state == ui2::UiRecordState::Unavailable)
      CHECK(ui2::UiRecordView::CursorTargetRect(data.focus).Empty());

    const ui2::UiCommand *instruction =
        FindTextCommand(scene.content.Stream(), test.text);
    REQUIRE(instruction != nullptr);
    CHECK(instruction->bounds.x ==
          120 - ui2::UiFont5x7::TextWidth(test.text.size()) / 2);
    CHECK(instruction->bounds.y == 164);
    CHECK(instruction->color == palette.Index(test.color));

    if (test.state == ui2::UiRecordState::Saving) {
      const ui2::UiCommand *progress =
          FindTextCommand(scene.content.Stream(), "42%");
      REQUIRE(progress != nullptr);
      CHECK(progress->bounds.x == 120 - ui2::UiFont5x7::TextWidth(3U, 2U) / 2);
      CHECK(progress->bounds.y == 132);
      CHECK(progress->color == palette.Index(ui2::UiColorToken::SystemWarning));
    }
  }
}

TEST_CASE("UI2 Record focus and state deltas match complete redraws") {
  CHECK(ui2::UiRecordView::CursorTargetRect(ui2::UiRecordFocus::Source) ==
        ui2::RectI16{7, 42, 226, 9});
  CHECK(ui2::UiRecordView::CursorTargetRect(ui2::UiRecordFocus::LineGain) ==
        ui2::RectI16{7, 53, 226, 9});
  CHECK(ui2::UiRecordView::CursorTargetRect(ui2::UiRecordFocus::MicGain) ==
        ui2::RectI16{7, 64, 226, 9});

  const ui2::UiRecordViewData previous;
  ui2::UiRecordViewData current = previous;
  current.focus = ui2::UiRecordFocus::MicGain;
  current.state = ui2::UiRecordState::Recording;
  current.elapsed = "00:09";
  CheckDeltaMatchesFullFrame(previous, current, ui2::UiRecordView::Build,
                             ui2::UiRecordView::RenderDelta);

  ui2::UiRecordViewData saving = current;
  saving.state = ui2::UiRecordState::Saving;
  saving.savingPercent = 81U;
  saving.meterAvailable = false;
  CheckDeltaMatchesFullFrame(current, saving, ui2::UiRecordView::Build,
                             ui2::UiRecordView::RenderDelta);
}

TEST_CASE("UI2 Sample Editor delta is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiSampleEditorViewData previous =
      ui2::test::ApprovedSampleEditorFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSampleEditorView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiSampleEditorViewData current = previous;
  current.name = "LIVE TAKE";
  current.start = "000128";
  current.end = "000512";
  current.field3Value = "PINGPONG";
  current.field4Value = "+3 DB";
  current.waveformRevision += 1;
  current.power = ui2::UiPowerState::BatteryLow;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 155, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSampleEditorView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSampleEditorView::RenderDelta(previous, current, currentScene, surface,
                                       palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Sample Slices delta is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiSampleSlicesViewData previous =
      ui2::test::ApprovedSampleSlicesFixture();
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSampleSlicesView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiSampleSlicesViewData current = previous;
  current.slice = "03 / 08";
  current.start = "000119";
  current.zoom = "2X";
  current.autoSliceApplyAvailable = false;
  current.selectedMarker = 3;
  current.waveformRevision += 1;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 149, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSampleSlicesView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSampleSlicesView::RenderDelta(previous, current, currentScene, surface,
                                       palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Sample pages keep the top bar free of legacy metadata") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;

  ui2::UiSampleEditorViewData editor;
  REQUIRE(ui2::UiSampleEditorView::Build(editor, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.top.Stream(), "EDIT") == nullptr);

  ui2::UiSampleSlicesViewData slices;
  REQUIRE(ui2::UiSampleSlicesView::Build(slices, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.top.Stream(), "04") == nullptr);
}

TEST_CASE("UI2 Sample pages remain clean while their state is idle") {
  ui2::UiPalette palette;
  ui2::UiSampleEditorViewData editor = ui2::test::ApprovedSampleEditorFixture();
  ui2::UiFrameScene editorScene;
  REQUIRE(ui2::UiSampleEditorView::Build(editor, palette, editorScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(editorScene, surface, palette);
  surface.ClearDirty();
  ui2::UiSampleEditorView::RenderDelta(editor, editor, editorScene, surface,
                                       palette);
  CHECK_FALSE(surface.DirtyTiles().Any());

  ui2::UiSampleSlicesViewData slices = ui2::test::ApprovedSampleSlicesFixture();
  ui2::UiFrameScene sliceScene;
  REQUIRE(ui2::UiSampleSlicesView::Build(slices, palette, sliceScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiFrameRenderer::RenderStatic(sliceScene, surface, palette);
  surface.ClearDirty();
  ui2::UiSampleSlicesView::RenderDelta(slices, slices, sliceScene, surface,
                                       palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 Sample Editor adapter owns controller text waveform and modes") {
  SampleEditorViewUi2Snapshot snapshot;
  CopyUi2SnapshotText(snapshot.name, "LIVE TAKE");
  CopyUi2SnapshotText(snapshot.start, "0000123");
  CopyUi2SnapshotText(snapshot.end, "0004567");
  CopyUi2SnapshotText(snapshot.operation, "PEAK NORMALIZE");
  std::copy(ui2::test::kApprovedSampleEditorWaveform.begin(),
            ui2::test::kApprovedSampleEditorWaveform.end(),
            snapshot.waveform.encoded.begin());
  snapshot.waveform.size = static_cast<std::uint16_t>(
      ui2::test::kApprovedSampleEditorWaveform.size());
  snapshot.waveform.revision = 0x12345678U;
  snapshot.waveformReady = true;
  snapshot.markers.Push(13U, Ui2WaveformMarkerKind::Start, false);
  snapshot.markers.Push(201U, Ui2WaveformMarkerKind::Playhead, false);
  snapshot.focus = SampleEditorViewUi2Focus::End;
  snapshot.focusDigit = 5U;
  snapshot.playing = true;
  snapshot.fileMutationAvailable = true;

  const ui2::UiSampleEditorControllerState state =
      ui2::MakeUiSampleEditorControllerState(
          snapshot, ui2::UiPowerState::BatteryHigh,
          {.enterHeld = true, .editHeld = false});
  snapshot.name[0] = 'X';
  snapshot.waveform.encoded[0] ^= 0xFFU;
  const ui2::UiSampleEditorViewData data = state.ToViewData();
  CHECK(data.name == "LIVE TAKE");
  CHECK(data.start == "0000123");
  CHECK(data.end == "0004567");
  CHECK(data.field3Label == "OP");
  CHECK(data.field3Value == "PEAK NORMALIZE");
  CHECK(data.field4Label == "APPLY");
  CHECK(data.waveformMask.size() ==
        ui2::test::kApprovedSampleEditorWaveform.size());
  CHECK(data.waveformMask.front() ==
        ui2::test::kApprovedSampleEditorWaveform.front());
  REQUIRE(data.markers.size() == 2U);
  CHECK(data.markers[0] ==
        ui2::UiSampleWaveformMarker{13U, ui2::UiSampleWaveformMarkerKind::Start,
                                    false});
  CHECK(data.markers[1].kind == ui2::UiSampleWaveformMarkerKind::Playhead);
  CHECK(data.cursor == ui2::UiSampleEditorCursor::End);
  CHECK(data.enterDigitFocus);
  CHECK(data.focusDigit == 5U);
  CHECK(ui2::UiSampleEditorView::CursorTargetRect(data) ==
        ui2::RectI16{120, 155, 9, 9});
  CHECK(data.power == ui2::UiPowerState::Playing);
  CHECK(data.help == "EDIT+ARROWS ADJUST END");
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSampleEditorView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.content.Stream(), "OP") != nullptr);
  CHECK(FindTextCommand(scene.content.Stream(), "PEAK NORMALIZE") != nullptr);
  CHECK(FindTextCommand(scene.content.Stream(), data.help) != nullptr);

  SampleEditorViewUi2Snapshot library = snapshot;
  library.focus = SampleEditorViewUi2Focus::SaveAndLoad;
  library.projectPool = false;
  const auto libraryState = ui2::MakeUiSampleEditorControllerState(library);
  const auto libraryData = libraryState.ToViewData();
  CHECK(libraryData.bottomActionCount == 3U);
  CHECK(libraryData.bottomActions[1] == "SAVE&LOAD");
  CHECK(libraryData.bottomActive == 1U);

  library.projectPool = true;
  library.focus = SampleEditorViewUi2Focus::Discard;
  const auto poolState = ui2::MakeUiSampleEditorControllerState(library);
  const auto poolData = poolState.ToViewData();
  CHECK(poolData.bottomActionCount == 2U);
  CHECK(poolData.bottomActions[1] == "DISCARD");
  CHECK(poolData.bottomActive == 1U);

  SampleEditorViewUi2Snapshot noMutation = snapshot;
  noMutation.fileMutationAvailable = false;
  noMutation.focus = SampleEditorViewUi2Focus::Operation;
  const auto noMutationState =
      ui2::MakeUiSampleEditorControllerState(noMutation);
  const auto noMutationData = noMutationState.ToViewData();
  CHECK(noMutationData.field4Label.empty());
  CHECK(noMutationData.help == "LEFT/RIGHT BROWSE (NO APPLY)");
  CHECK(noMutationData.cursor == ui2::UiSampleEditorCursor::Field3);
  CHECK(noMutationData.bottomActionCount == 1U);
  CHECK(noMutationData.bottomActions[0] == "DISCARD");
  ui2::UiFrameScene noMutationScene;
  REQUIRE(ui2::UiSampleEditorView::Build(noMutationData, palette,
                                         noMutationScene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(noMutationScene.content.Stream(), "APPLY") == nullptr);
  CHECK(FindTextCommand(noMutationScene.bottom.Stream(), "SAVE") == nullptr);
  CHECK(FindTextCommand(noMutationScene.bottom.Stream(), "DISCARD") !=
        nullptr);
}

TEST_CASE("UI2 Sample Slices adapter maps real markers focus and help") {
  SampleSlicesViewUi2Snapshot snapshot;
  CopyUi2SnapshotText(snapshot.slice, "02 / 03");
  CopyUi2SnapshotText(snapshot.start, "0000064");
  CopyUi2SnapshotText(snapshot.zoom, "4X");
  std::copy(ui2::test::kApprovedSampleSlicesWaveform.begin(),
            ui2::test::kApprovedSampleSlicesWaveform.end(),
            snapshot.waveform.encoded.begin());
  snapshot.waveform.size = static_cast<std::uint16_t>(
      ui2::test::kApprovedSampleSlicesWaveform.size());
  snapshot.waveform.revision = 0xCAFE1234U;
  snapshot.waveformReady = true;
  snapshot.markers.Push(9U, Ui2WaveformMarkerKind::Slice, false);
  snapshot.markers.Push(103U, Ui2WaveformMarkerKind::Slice, true);
  snapshot.markers.Push(177U, Ui2WaveformMarkerKind::Playhead, false);
  snapshot.selectedSlice = 1U;
  snapshot.autoSliceCount = 16U;
  snapshot.hasSample = true;
  snapshot.previewActive = true;
  snapshot.previewPlayheadVisible = true;
  snapshot.focus = SampleSlicesViewUi2Focus::Waveform;

  const ui2::UiSampleSlicesControllerState state =
      ui2::MakeUiSampleSlicesControllerState(
          snapshot, ui2::UiPowerState::BatteryNormal,
          {.enterHeld = false, .editHeld = true});
  snapshot.slice[0] = 'X';
  snapshot.markers.markers[1].x = 0U;
  const ui2::UiSampleSlicesViewData data = state.ToViewData();
  CHECK(data.slice == "02 / 03");
  CHECK(data.start == "0000064");
  CHECK(data.zoom == "4X");
  CHECK(data.autoSliceCount == "16");
  REQUIRE(data.markers.size() == 3U);
  CHECK(data.markers[1].x == 103U);
  CHECK(data.markers[1].selected);
  CHECK(data.markers[2].kind == ui2::UiSampleWaveformMarkerKind::Playhead);
  CHECK(data.cursor == ui2::UiSampleSlicesCursor::Waveform);
  CHECK(ui2::UiSampleSlicesView::CursorTargetRect(data) ==
        ui2::RectI16{7, 43, 226, 86});
  CHECK(data.help == "UP/DOWN ZOOM");
  CHECK(data.power == ui2::UiPowerState::Playing);

  const ui2::UiSampleSlicesControllerState copy = state;
  CHECK(copy == state);
  const ui2::UiSampleSlicesViewData copyData = copy.ToViewData();
  CHECK(copyData.waveformMask.data() != data.waveformMask.data());

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSampleSlicesView::Build(data, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiFrameScene copyScene;
  REQUIRE(ui2::UiSampleSlicesView::Build(copyData, palette, copyScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSampleSlicesView::RenderDelta(data, copyData, copyScene, surface,
                                       palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}

TEST_CASE("UI2 Sample marker changes use pixel-identical deltas") {
  std::array<ui2::UiSampleWaveformMarker, 2> previousMarkers{{
      {21U, ui2::UiSampleWaveformMarkerKind::Start, true},
      {180U, ui2::UiSampleWaveformMarkerKind::End, false},
  }};
  std::array<ui2::UiSampleWaveformMarker, 2> currentMarkers = previousMarkers;
  currentMarkers[0].x = 45U;
  currentMarkers[0].selected = false;
  currentMarkers[1].selected = true;

  ui2::UiSampleEditorViewData previous =
      ui2::test::ApprovedSampleEditorFixture();
  previous.markers = previousMarkers;
  ui2::UiSampleEditorViewData current = previous;
  current.markers = currentMarkers;
  CheckDeltaMatchesFullFrame(previous, current, ui2::UiSampleEditorView::Build,
                             ui2::UiSampleEditorView::RenderDelta);
}

TEST_CASE("UI2 Dialog overlay preserves its page and suppresses Bottom Bar") {
  ui2::UiPalette palette;
  ui2::UiSongViewData song = ui2::test::ApprovedSongFixture();
  song.playing = false;
  song.power = ui2::UiPowerState::BatteryNormal;
  song.showVu = false;
  song.showBottom = false;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(song, palette, scene) ==
          ui2::UiBuildStatus::Built);
  const std::size_t baseCommands = scene.content.Size();
  ui2::UiDialogViewData dialog;
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) == ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.bottomVisible);
  CHECK(scene.top.Size() > 0);
  CHECK(scene.content.Size() == baseCommands);
  CHECK(scene.overlay.Size() > 0);
}

TEST_CASE("UI2 feedback overlay preserves bars and exposes no actions") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(ui2::test::ApprovedSongFixture(), palette,
                                 scene) == ui2::UiBuildStatus::Built);
  const std::size_t baseCommands = scene.content.Size();
  const std::size_t bottomCommands = scene.bottom.Size();

  ui2::UiDialogViewData feedback;
  feedback.kind = ui2::UiDialogKind::Feedback;
  feedback.tone = ui2::UiDialogTone::Error;
  feedback.title = "NO FREE TABLE";
  REQUIRE(ui2::UiDialogView::Apply(feedback, scene) ==
          ui2::UiBuildStatus::Built);

  CHECK(scene.bottomVisible);
  CHECK(scene.content.Size() == baseCommands);
  CHECK(scene.bottom.Size() == bottomCommands);
  CHECK(scene.overlay.Size() == 3U);
  const ui2::UiCommandStream stream = scene.overlay.Stream();
  const std::string_view text(stream.text.data(), stream.text.size());
  CHECK(text.find("NO FREE TABLE") != std::string_view::npos);
  CHECK(ui2::UiDialogView::DamageRect(ui2::UiDialogKind::Feedback) ==
        ui2::RectI16{12, 184, 216, 18});
}

TEST_CASE("UI2 Dialog fits every live base-page scene") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiDialogViewData dialog;
  dialog.kind = ui2::UiDialogKind::Message;
  dialog.title = "DIAGNOSTIC MESSAGE";
  dialog.label = "SECOND LINE";
  dialog.actions = {ui2::UiDialogAction::Ok, ui2::UiDialogAction::Cancel,
                    ui2::UiDialogAction::Yes, ui2::UiDialogAction::No};
  dialog.actionCount = 2;

  const auto apply = [&] {
    REQUIRE(ui2::UiDialogView::Apply(dialog, scene) ==
            ui2::UiBuildStatus::Built);
  };
  REQUIRE(ui2::UiSongView::Build(ui2::test::ApprovedSongFixture(), palette,
                                 scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiChainView::Build(ui2::test::ApprovedChainFixture(), palette,
                                  scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiPhraseView::Build(ui2::test::ApprovedPhraseFixture("fx"),
                                   palette,
                                   scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiTableView::Build(ui2::test::ApprovedTableFixture("instrument"),
                                  palette, scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiInstrumentView::Build(
              ui2::test::ApprovedInstrumentFixture("opal"), palette, scene) ==
          ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiDeviceView::Build(ui2::test::ApprovedDeviceFixture(), palette,
                                   scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(
      ui2::UiBrowserView::Build(ui2::test::ApprovedBrowserFixture("projects"),
                                palette, scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiGrooveView::Build(ui2::test::ApprovedGrooveFixture(), palette,
                                   scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiMixerView::Build(ui2::test::ApprovedMixerFixture(), palette,
                                  scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiSampleEditorView::Build(
              ui2::test::ApprovedSampleEditorFixture(), palette, scene) ==
          ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiSampleSlicesView::Build(
              ui2::test::ApprovedSampleSlicesFixture(), palette, scene) ==
          ui2::UiBuildStatus::Built);
  apply();
}

TEST_CASE("UI2 base-page delta remains exact beneath a retained dialog") {
  ui2::UiPalette palette;
  ui2::UiDialogViewData dialog;
  dialog.kind = ui2::UiDialogKind::Message;
  dialog.title = "STAY VISIBLE";
  dialog.actions[0] = ui2::UiDialogAction::Ok;
  dialog.actionCount = 1;

  ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiSongView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(ui2::UiDialogView::Apply(dialog, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage actualStorage;
  ui2::UiIndexedSurface actual(actualStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, actual, palette);
  actual.ClearDirty();

  ui2::UiSongViewData current = previous;
  current.elapsed = "12:34";
  current.rows[0][0] = 0x4a;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSongView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(ui2::UiDialogView::Apply(dialog, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSongView::RenderDelta(previous, current, currentScene, actual,
                               palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(actual.Pixels().begin(), actual.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Dialog renders real lines actions and selected action") {
  ui2::UiDialogViewData dialog;
  dialog.kind = ui2::UiDialogKind::Message;
  dialog.title = "DELETE PROJECT?";
  dialog.label = "THIS CANNOT BE UNDONE";
  dialog.actions = {ui2::UiDialogAction::Yes, ui2::UiDialogAction::No,
                    ui2::UiDialogAction::Ok, ui2::UiDialogAction::Cancel};
  dialog.actionCount = 2;
  dialog.selectedAction = 1;
  dialog.actionsFocused = true;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) == ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream stream = scene.overlay.Stream();
  const std::string_view text(stream.text.data(), stream.text.size());
  CHECK(text.find("DELETE PROJECT?") != std::string_view::npos);
  CHECK(text.find("THIS CANNOT BE UNDONE") != std::string_view::npos);
  CHECK(text.find("YES") != std::string_view::npos);
  CHECK(text.find("NO") != std::string_view::npos);
  CHECK(std::count_if(stream.commands.begin(), stream.commands.end(),
                      [](const ui2::UiCommand &command) {
                        return command.kind ==
                               ui2::UiCommandKind::FillCoverageRoundedRect;
                      }) == 1);

  dialog.actionsFocused = false;
  scene.Clear();
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) == ui2::UiBuildStatus::Built);
  CHECK(std::none_of(
      scene.overlay.Commands().begin(), scene.overlay.Commands().end(),
      [](const ui2::UiCommand &command) {
        return command.kind == ui2::UiCommandKind::FillCoverageRoundedRect;
      }));
}

TEST_CASE("UI2 Text Input retains its default action accent while editing") {
  ui2::UiDialogViewData input;
  input.kind = ui2::UiDialogKind::TextInput;
  input.title = "RENAME";
  input.label = "NAME";
  input.value = "ONECYCAC";
  input.actions = {ui2::UiDialogAction::Ok,
                   ui2::UiDialogAction::Cancel,
                   ui2::UiDialogAction::Yes,
                   ui2::UiDialogAction::No};
  input.actionCount = 2;
  input.selectedAction = 0;
  input.actionsFocused = false;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(input, scene) ==
          ui2::UiBuildStatus::Built);
  const ui2::UiCommand *ok = FindTextCommand(scene.overlay.Stream(), "OK");
  const ui2::UiCommand *cancel =
      FindTextCommand(scene.overlay.Stream(), "CANCEL");
  REQUIRE(ok != nullptr);
  REQUIRE(cancel != nullptr);
  CHECK(ok->color == static_cast<ui2::PaletteIndex>(
                         ui2::UiColorToken::TextColored));
  CHECK(cancel->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextDim));
}

TEST_CASE("UI2 Render Progress keeps one status line plus elapsed") {
  ui2::UiDialogViewData render;
  render.kind = ui2::UiDialogKind::RenderProgress;
  render.title = "DIAGNOSTIC";
  render.label = "RENDERING";
  render.elapsed = "00:08";
  render.actions[0] = ui2::UiDialogAction::Cancel;
  render.actionCount = 1;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(render, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(FindTextCommand(scene.overlay.Stream(), "DIAGNOSTIC") == nullptr);
  const ui2::UiCommand *status =
      FindTextCommand(scene.overlay.Stream(), "RENDERING");
  const ui2::UiCommand *elapsed =
      FindTextCommand(scene.overlay.Stream(), "00:08");
  REQUIRE(status != nullptr);
  REQUIRE(elapsed != nullptr);
  CHECK(status->bounds.y == 91);
  CHECK(elapsed->bounds.y == 108);
}

TEST_CASE("UI2 Dialog uses full-screen and render snapshot text") {
  ui2::UiDialogViewData full;
  full.kind = ui2::UiDialogKind::FullScreen;
  full.title = "LOW BATTERY";
  full.label = "CONNECT CHARGER";
  ui2::UiFrameScene fullScene;
  REQUIRE(ui2::UiDialogView::Apply(full, fullScene) ==
          ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream fullStream = fullScene.overlay.Stream();
  const std::string_view fullText(fullStream.text.data(),
                                  fullStream.text.size());
  CHECK(fullText.find("LOW BATTERY") != std::string_view::npos);
  CHECK(fullText.find("CONNECT CHARGER") != std::string_view::npos);

  ui2::UiDialogViewData render;
  render.kind = ui2::UiDialogKind::RenderProgress;
  render.title = "STEMS RENDERING";
  render.label = "RENDER COMPLETE!";
  render.elapsed = "100%";
  render.progressWidth = 144;
  render.actions[0] = ui2::UiDialogAction::Ok;
  render.actionCount = 1;
  ui2::UiFrameScene renderScene;
  REQUIRE(ui2::UiDialogView::Apply(render, renderScene) ==
          ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream renderStream = renderScene.overlay.Stream();
  const std::string_view renderText(renderStream.text.data(),
                                    renderStream.text.size());
  CHECK(renderText.find("STEMS RENDERING") == std::string_view::npos);
  CHECK(renderText.find("RENDER COMPLETE!") != std::string_view::npos);
  CHECK(renderText.find("100%") != std::string_view::npos);
  CHECK(renderText.find("OK") != std::string_view::npos);
  CHECK(renderText.find("CANCEL") == std::string_view::npos);
}

TEST_CASE("UI2 Dialog delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette palette;
  ui2::UiSongViewData song = ui2::test::ApprovedSongFixture();
  song.playing = false;
  song.power = ui2::UiPowerState::BatteryNormal;
  song.showVu = false;
  song.showBottom = false;
  ui2::UiDialogViewData previous;
  previous.kind = ui2::UiDialogKind::RenderProgress;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(song, palette, scene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(ui2::UiDialogView::Apply(previous, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();

  ui2::UiDialogViewData current = previous;
  current.elapsed = "00:12";
  current.progressWidth = 120;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSongView::Build(song, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(ui2::UiDialogView::Apply(current, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiDialogView::RenderDelta(previous, current, currentScene, surface,
                                 palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 full-screen diagnostic replaces every retained page layer") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(ui2::test::ApprovedSongFixture(), palette,
                                 scene) == ui2::UiBuildStatus::Built);
  ui2::UiDialogViewData dialog;
  dialog.kind = ui2::UiDialogKind::FullScreen;
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) == ui2::UiBuildStatus::Built);
  CHECK(scene.topHeight == 0);
  CHECK_FALSE(scene.bottomVisible);
  CHECK(scene.top.Size() == 0);
  CHECK(scene.bottom.Size() == 0);
  CHECK(scene.content.Size() == 0);
  CHECK(scene.overlay.Size() == 4);
}

TEST_CASE(
    "UI2 retained full-screen dialog is independent of hidden base state") {
  ui2::UiPalette palette;
  ui2::UiDialogViewData dialog;
  dialog.kind = ui2::UiDialogKind::FullScreen;
  dialog.title = "LOW BATTERY";
  dialog.label = "CONNECT CHARGER";

  ui2::UiSongViewData first = ui2::test::ApprovedSongFixture();
  ui2::UiFrameScene firstScene;
  REQUIRE(ui2::UiSongView::Build(first, palette, firstScene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(ui2::UiDialogView::Apply(dialog, firstScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage firstStorage;
  ui2::UiIndexedSurface firstSurface(firstStorage);
  ui2::UiFrameRenderer::RenderStatic(firstScene, firstSurface, palette);

  ui2::UiSongViewData changed = first;
  changed.elapsed = "59:59";
  changed.rows[0][0] = 0x7f;
  changed.playing = !first.playing;
  ui2::UiFrameScene changedScene;
  REQUIRE(ui2::UiSongView::Build(changed, palette, changedScene) ==
          ui2::UiBuildStatus::Built);
  REQUIRE(ui2::UiDialogView::Apply(dialog, changedScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage changedStorage;
  ui2::UiIndexedSurface changedSurface(changedStorage);
  ui2::UiFrameRenderer::RenderStatic(changedScene, changedSurface, palette);

  CHECK(std::equal(firstSurface.Pixels().begin(), firstSurface.Pixels().end(),
                   changedSurface.Pixels().begin(),
                   changedSurface.Pixels().end()));
}

TEST_CASE("UI2 Dialog idle frame stays clean") {
  ui2::UiPalette palette;
  ui2::UiSongViewData song = ui2::test::ApprovedSongFixture();
  song.showVu = false;
  song.showBottom = false;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiSongView::Build(song, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiDialogViewData dialog;
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) == ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiDialogView::RenderDelta(dialog, dialog, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}
