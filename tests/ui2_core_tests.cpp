#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Animation/UiTransitionTimeline.h"
#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Render/UiRasterizer.h"
#include "UI2/Render/UiRgb565Presenter.h"
#include "UI2/Render/UiVuGradient.h"
#include "UI2/Scene/UiCommandList.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/Theme/UiThemeSchema.h"
#include "UI2/UiEngine.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Browser/UiBrowserView.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Device/UiDeviceView.h"
#include "UI2/Views/Dialog/UiDialogView.h"
#include "UI2/Views/Font/UiFontView.h"
#include "UI2/Views/Groove/UiGrooveView.h"
#include "UI2/Views/Song/UiSongView.h"
#include "UI2/Views/Phrase/UiPhraseView.h"
#include "UI2/Views/Instrument/UiInstrumentView.h"
#include "UI2/Views/Mixer/UiMixerView.h"
#include "UI2/Views/Project/UiProjectView.h"
#include "UI2/Views/Record/UiRecordView.h"
#include "UI2/Views/Sample/UiSampleViews.h"
#include "UI2/Views/Table/UiTableView.h"
#include "UI2/Views/Theme/UiThemeView.h"
#include "UI2/Views/Tracker/UiTrackerGridMetrics.h"
#include "Adapters/wasm/gui/WasmUiPresenter.h"
#include "Application/UI2/Ui2SettingsAdapters.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"
#include "Application/UI2/Ui2ModalInputGate.h"

#include "ui2_song_fixture.h"
#include "ui2_browser_fixture.h"
#include "ui2_chain_fixture.h"
#include "ui2_device_fixture.h"
#include "ui2_groove_fixture.h"
#include "ui2_phrase_fixture.h"
#include "ui2_instrument_fixture.h"
#include "ui2_mixer_fixture.h"
#include "ui2_project_fixture.h"
#include "ui2_sample_fixture.h"
#include "ui2_table_fixture.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

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
      probe.records[index] =
          {x, y, width, height, pixels[0], pixels[width * height - 1U]};
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
  REQUIRE(build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface delta(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, delta, palette);
  delta.ClearDirty();

  ui2::UiFrameScene currentScene;
  REQUIRE(build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  renderDelta(previous, current, currentScene, delta, palette);

  ui2::UiSurfaceStorage fullStorage;
  ui2::UiIndexedSurface full(fullStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, full, palette);
  CHECK(std::equal(delta.Pixels().begin(), delta.Pixels().end(),
                   full.Pixels().begin(), full.Pixels().end()));
}

} // namespace

TEST_CASE("UI2 geometry clips and unions signed pixel rectangles") {
  CHECK(ui2::Intersect({-4, 2, 10, 8}, ui2::RectI16::Screen()) ==
        ui2::RectI16{0, 2, 6, 8});
  CHECK(ui2::Intersect({250, 2, 4, 8}, ui2::RectI16::Screen()).Empty());
  CHECK(ui2::Union({3, 4, 5, 6}, {1, 7, 10, 2}) ==
        ui2::RectI16{1, 4, 10, 6});
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

TEST_CASE("UI2 rounded bubble keeps straight edges crisp and softens corners only") {
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
  CHECK(palette.Get(
            palette.AntialiasIndex(ui2::UiCoverage::Cursor, 1)) ==
        ui2::Rgb888{0x14, 0x42, 0x42});
  CHECK(palette.Get(
            palette.AntialiasIndex(ui2::UiCoverage::Cursor, 2)) ==
        ui2::Rgb888{0x24, 0x75, 0x79});
  CHECK(palette.Get(
            palette.AntialiasIndex(ui2::UiCoverage::Cursor, 3)) ==
        ui2::Rgb888{0x35, 0xA9, 0xB1});
  CHECK(palette.Get(
            palette.AntialiasIndex(ui2::UiCoverage::Playback, 3)) ==
        ui2::Rgb888{0x4F, 0xB0, 0x76});
}

TEST_CASE("UI2 user palette exposes exactly the approved semantic fields") {
  CHECK(ui2::UiPalette::kUserColorCount == 19);
  CHECK(ui2::kUiThemeColors.size() == ui2::UiPalette::kUserColorCount);
  CHECK(ui2::kUiThemeColors.front().key == "surface.bg");
  CHECK(ui2::kUiThemeColors.back().key == "vu.peak");
  for (std::size_t left = 0; left < ui2::kUiThemeColors.size(); ++left) {
    CHECK(static_cast<std::size_t>(ui2::kUiThemeColors[left].token) == left);
    for (std::size_t right = left + 1; right < ui2::kUiThemeColors.size();
         ++right) {
      CHECK(ui2::kUiThemeColors[left].key != ui2::kUiThemeColors[right].key);
    }
  }
}

TEST_CASE("UI2 user colors remain independent while element colors regenerate") {
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
            palette.Index(ui2::UiColorToken::SurfaceBackground))) !=
        oldCorner);
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
  const auto background =
      palette.Index(ui2::UiColorToken::SurfaceBackground);
  CHECK(surface.Pixel(0, 0) == background);
  CHECK(surface.Pixel(239, 239) == background);
}

TEST_CASE("UI2 tracker pages share one ten-pixel vertical rhythm") {
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
}

TEST_CASE("UI2 tracker selections resolve to one clipped rounded region") {
  CHECK(ui2::UiSongView::SelectionTargetRect(1, 18, 3, 21, 16) ==
        ui2::RectI16{47, 67, 57, 39});
  CHECK(ui2::UiSongView::SelectionTargetRect(0, 0, 7, 15, 16).Empty());
  CHECK(ui2::UiChainView::SelectionTargetRect(0, 2, 1, 4) ==
        ui2::RectI16{26, 67, 36, 29});
  CHECK(ui2::UiPhraseView::SelectionTargetRect(1, 1, 4, 3) ==
        ui2::RectI16{59, 57, 108, 29});
  // Legacy Table selection may transiently report column 6. UI2 clips that
  // endpoint to the sixth visible value column without escaping the screen.
  CHECK(ui2::UiTableView::SelectionTargetRect(2, 0, 6, 15) ==
        ui2::RectI16{90, 47, 119, 159});
}

TEST_CASE("UI2 tracker selection deltas match complete redraws") {
  {
    const ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
    ui2::UiSongViewData current = previous;
    current.selectionVisualRect =
        ui2::UiSongView::SelectionTargetRect(0, 8, 3, 11, 0);
    CheckDeltaMatchesFullFrame(
        previous, current, ui2::UiSongView::Build,
        ui2::UiSongView::RenderDelta);
  }
  {
    const ui2::UiChainViewData previous = ui2::test::ApprovedChainFixture();
    ui2::UiChainViewData current = previous;
    current.selectionVisualRect =
        ui2::UiChainView::SelectionTargetRect(0, 0, 1, 5);
    CheckDeltaMatchesFullFrame(
        previous, current, ui2::UiChainView::Build,
        ui2::UiChainView::RenderDelta);
  }
  {
    const ui2::UiPhraseViewData previous =
        ui2::test::ApprovedPhraseFixture("note");
    ui2::UiPhraseViewData current = previous;
    current.selectionVisualRect =
        ui2::UiPhraseView::SelectionTargetRect(1, 2, 5, 6);
    CheckDeltaMatchesFullFrame(
        previous, current, ui2::UiPhraseView::Build,
        ui2::UiPhraseView::RenderDelta);
  }
  {
    const ui2::UiTableViewData previous =
        ui2::test::ApprovedTableFixture("phrase");
    ui2::UiTableViewData current = previous;
    current.selectionVisualRect =
        ui2::UiTableView::SelectionTargetRect(0, 4, 5, 9);
    CheckDeltaMatchesFullFrame(
        previous, current, ui2::UiTableView::Build,
        ui2::UiTableView::RenderDelta);
  }
}

TEST_CASE("UI2 vertical list reveals items and reconciles contextual bars") {
  ui2::UiThemeViewData previous;
  ui2::UiThemeViewData current = previous;
  current.selectedColor = 18;
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
  CHECK(surface.Pixel(0, 0) ==
        palette.Index(ui2::UiColorToken::SurfaceTopBar));
  CHECK(surface.Pixel(0, 239) ==
        palette.Index(ui2::UiColorToken::SurfaceBackground));
}

TEST_CASE("UI2 sparse coverage masks copy bounded data and decode columns") {
  ui2::UiPalette palette;
  ui2::UiContentScene scene;
  ui2::UiSceneBuilder<256, 1024> builder(scene);
  builder.Fill({10, 10, 20, 10}, ui2::UiColorToken::DerivedVuTrack);
  std::array<std::uint8_t, 11> encoded{
      0x00, 0x01, 0x00, 0x02, 0x04, 0x39,
      0xFF, 0x00, 0x05, 0x02, 0x07};
  builder.SparseCoverageMask({10, 10, 4, 10}, encoded,
                             ui2::UiCoverage::Cursor,
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

TEST_CASE("UI2 bar resolver applies the documented central priority") {
  ui2::UiBottomBarModel page{.kind = ui2::UiBottomBarKind::Hidden};
  ui2::UiBottomBarModel cursor{.kind = ui2::UiBottomBarKind::Context};
  ui2::UiBottomBarModel modal{.kind = ui2::UiBottomBarKind::Actions};
  ui2::UiTrackNotesModel tracks{};
  tracks.selectedTrack = 2;
  ui2::UiBarInputs inputs{
      .pageTop = {.title = "PHRASE", .meta = "3A"},
      .pageDefault = page,
      .cursorContext = &cursor,
      .criticalModal = &modal,
      .editHeldTracks = &tracks,
      .editHeldNumber = true,
      .navHeld = true,
  };
  const ui2::UiResolvedChrome resolved = ui2::UiBarResolver::Resolve(inputs);
  CHECK(resolved.top.metaSelected);
  CHECK(resolved.top.power == ui2::UiPowerState::Navigation);
  CHECK(resolved.bottom.kind == ui2::UiBottomBarKind::Actions);
}

TEST_CASE("UI2 NAV targets share one movable seven by nine bubble") {
  ui2::UiBarScene scene;
  CHECK(ui2::UiChromeRenderer::BuildTop(
            {.title = "SONG",
             .meta = "ONECYCAC",
             .power = ui2::UiPowerState::Navigation,
             .navTarget = ui2::UiNavTarget::Song},
            scene) == ui2::UiBuildStatus::Built);
  CHECK(ui2::UiChromeRenderer::BuildTop(
            {.title = "SONG",
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

TEST_CASE("UI2 content slides as whole layers while bars crossfade") {
  ui2::UiTransitionTimeline timeline;
  timeline.StartContent(ui2::UiSlideDirection::Left, 1'000);
  timeline.StartBarFade(1'000);
  const ui2::UiLayerOffsets quarter = timeline.Content(1'045);
  CHECK(quarter.outgoing.x < -60);
  CHECK(quarter.incoming.x < 180);
  CHECK(quarter.outgoing.y == 0);
  const ui2::UiCrossfadeOpacity fade = timeline.BarFade(1'030);
  CHECK(fade.incoming > 16'383);
  CHECK(fade.outgoing < 49'152);
  CHECK(timeline.Content(1'180).incoming == ui2::PointI16{0, 0});
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
  CHECK(surface.Pixel(0, 0) ==
        palette.Index(ui2::UiColorToken::SurfaceTopBar));
  CHECK(surface.Pixel(5, 34) ==
        palette.Index(ui2::UiColorToken::SurfaceBackground));
  CHECK(surface.Pixel(5, 128) ==
        palette.Index(ui2::UiColorToken::CursorRow));
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
  std::array<std::uint16_t, ui2::UiRgb565Presenter::kTransferPixels>
      transfer{};
  Rgb565WriteProbe probe;
  ui2::UiRgb565Presenter presenter(
      transfer.data(), transfer.size(), &Rgb565WriteProbe::Write, &probe,
      ui2::UiRgb565ByteOrder::MostSignificantByteFirst);
  CHECK(presenter.Present(surface, palette, strips) ==
        ui2::PresentResult::Presented);
  REQUIRE(probe.calls == 2);
  CHECK(probe.records[0].x == 4);
  CHECK(probe.records[0].y == 5);
  CHECK(probe.records[0].width == 3);
  CHECK(probe.records[0].height == 8);
  CHECK(probe.records[1].y == 13);
  CHECK(probe.records[1].height == 2);

  const std::uint16_t cursor565 = palette.Rgb565(cursor);
  const std::uint16_t field565 = palette.Rgb565(field);
  CHECK(probe.records[0].first ==
        static_cast<std::uint16_t>((cursor565 >> 8U) | (cursor565 << 8U)));
  CHECK(probe.records[0].last ==
        static_cast<std::uint16_t>((field565 >> 8U) | (field565 << 8U)));

  probe = {};
  probe.failOnCall = 2;
  CHECK(presenter.Present(surface, palette, strips) ==
        ui2::PresentResult::Deferred);
  CHECK(probe.calls == 2);

  std::array<std::uint16_t, 1> undersized{};
  Rgb565WriteProbe rejectedProbe;
  ui2::UiRgb565Presenter rejected(
      undersized.data(), undersized.size(), &Rgb565WriteProbe::Write,
      &rejectedProbe, ui2::UiRgb565ByteOrder::Native);
  CHECK(rejected.Present(surface, palette, strips) ==
        ui2::PresentResult::Failed);
  CHECK(rejectedProbe.calls == 0);
}

TEST_CASE("UI2 VU mapping is bounded monotonic and integer only") {
  CHECK(ui2::UiApplicationRuntime::VuTopFromAmplitude(0) == 153);
  CHECK(ui2::UiApplicationRuntime::VuTopFromAmplitude(32) == 153);
  CHECK(ui2::UiApplicationRuntime::VuTopFromAmplitude(32700) == 0);
  std::uint8_t previous = 153;
  for (std::uint32_t amplitude = 33; amplitude <= 32767; amplitude += 37) {
    const std::uint8_t top = ui2::UiApplicationRuntime::VuTopFromAmplitude(
        static_cast<std::uint16_t>(amplitude));
    CHECK(top <= previous);
    CHECK(top <= 153);
    previous = top;
  }
}

TEST_CASE("UI2 firmware runtime keeps a fixed bounded memory footprint") {
  // 64-bit Host is the larger layout; the ESP32-S3 build uses 32-bit pointers.
  CHECK(sizeof(ui2::UiApplicationRuntime) < 74'000);
  CHECK(sizeof(ui2::UiRgb565Presenter) <= 64);
  CHECK(ui2::UiRgb565Presenter::kTransferPixels * sizeof(std::uint16_t) ==
        3'840);
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

TEST_CASE("UI2 Song delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette deltaPalette;
  ui2::UiSongViewData previous = ui2::test::ApprovedSongFixture();
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiSongView::Build(previous, deltaPalette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface deltaSurface(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, deltaSurface,
                                     deltaPalette);
  deltaSurface.ClearDirty();

  ui2::UiSongViewData current = previous;
  current.name = "NEXTSONG";
  current.elapsed = "00:09";
  current.editRow = 9;
  current.editTrack = 3;
  current.rows[4][2] = 0x7A;
  current.notes[5] = "A#4";
  current.playbackRows[1] = 12;
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
  ui2::UiFrameRenderer::RenderStatic(previousScene, deltaSurface,
                                     deltaPalette);
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
        ui2::RectI16{131, 77, 15, 9});
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 5'000);
  CHECK(transferredPixels < 240U * 240U / 10U);
}

TEST_CASE("UI2 Phrase delta rendering is pixel-identical to a full redraw") {
  ui2::UiPalette deltaPalette;
  ui2::UiPhraseViewData previous =
      ui2::test::ApprovedPhraseFixture("note");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiPhraseView::Build(previous, deltaPalette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage deltaStorage;
  ui2::UiIndexedSurface deltaSurface(deltaStorage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, deltaSurface,
                                     deltaPalette);
  deltaSurface.ClearDirty();

  ui2::UiPhraseViewData current = previous;
  current.editRow = 4;
  current.editColumn = 2;
  current.activeHeader = ui2::UiPhraseHeader::Fx1;
  current.rows[7][3] = "BEEF";
  current.cursorBottom =
      ui2::test::ApprovedPhraseFixture("fx").cursorBottom;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {75, 76, 20, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiPhraseView::Build(current, deltaPalette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiPhraseView::RenderDelta(previous, current, currentScene,
                                 deltaSurface, deltaPalette);

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

TEST_CASE("UI2 Phrase dual cursor animation renders exact visual overrides") {
  ui2::UiPalette palette;
  ui2::UiPhraseViewData data =
      ui2::test::ApprovedPhraseFixture("number");
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
        ui2::RectI16{68, 212, 15, 9});
}

TEST_CASE("UI2 Phrase idle is clean and a cursor move stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiPhraseViewData previous =
      ui2::test::ApprovedPhraseFixture("note");
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
  CHECK(transferredPixels < 240U * 240U / 7U);
}

TEST_CASE("UI2 Phrase animated bottom cursor delta matches a full frame") {
  ui2::UiPalette palette;
  ui2::UiPhraseViewData previous =
      ui2::test::ApprovedPhraseFixture("number");
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
  ui2::UiTableViewData previous =
      ui2::test::ApprovedTableFixture("phrase");
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

TEST_CASE("UI2 Table idle is clean and row motion stays locally dirty") {
  ui2::UiPalette palette;
  ui2::UiTableViewData previous =
      ui2::test::ApprovedTableFixture("phrase");
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
  CHECK(transferredPixels < 240U * 240U / 7U);
}

TEST_CASE("UI2 Instrument delta rendering is pixel-identical to a full redraw") {
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
TEST_CASE("UI2 Instrument idle is clean and cursor motion stays locally dirty") {
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

  ui2::UiInstrumentView::RenderDelta(previous, previous, previousScene,
                                     surface, palette);
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
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

TEST_CASE("UI2 Instrument exposes fixed cursor targets for fields and OPAL operators") {
  ui2::UiInstrumentViewData sample =
      ui2::test::ApprovedInstrumentFixture("sample");
  sample.cursor = ui2::UiInstrumentCursor::Field;
  sample.selectedField = 3;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(sample) ==
        ui2::RectI16{7, 95, 226, 9});

  ui2::UiInstrumentViewData opal =
      ui2::test::ApprovedInstrumentFixture("opal");
  opal.selectedOperator = 2;
  opal.cursor = ui2::UiInstrumentCursor::Operator1;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(opal) ==
        ui2::RectI16{139, 161, 40, 9});
  opal.cursor = ui2::UiInstrumentCursor::Operator2;
  CHECK(ui2::UiInstrumentView::CursorTargetRect(opal) ==
        ui2::RectI16{185, 161, 40, 9});
}

TEST_CASE("UI2 Instrument field focus delta is pixel-identical to a full redraw") {
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
  CHECK(ui2::UiMixerView::MeterDamageRect(0, 0) ==
        ui2::RectI16{7, 46, 7, 153});
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 3'000);
}

TEST_CASE("UI2 Chain keeps stereo VU channels physically separate") {
  CHECK(ui2::UiChainView::RowDamageRect(0) ==
        ui2::RectI16{5, 47, 213, 11});
  CHECK(ui2::UiChainView::VuDamageRect(0) ==
        ui2::RectI16{219, 47, 7, 153});
  CHECK(ui2::UiChainView::VuDamageRect(1) ==
        ui2::RectI16{228, 47, 7, 153});
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 8'000);
}

TEST_CASE("UI2 Project resolves cursor-specific bottom bars") {
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiProjectView::Build(
              ui2::test::ApprovedProjectFixture("name"), palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  REQUIRE(ui2::UiProjectView::Build(
              ui2::test::ApprovedProjectFixture("playback"), palette,
              scene) == ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.bottomVisible);
  REQUIRE(ui2::UiProjectView::Build(
              ui2::test::ApprovedProjectFixture("cleanup"), palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.bottomVisible);
  REQUIRE(ui2::UiProjectView::Build(
              ui2::test::ApprovedProjectFixture("render"), palette, scene) ==
          ui2::UiBuildStatus::Built);
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
    CHECK_FALSE(scene.bottomVisible);
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
  ui2::UiProjectViewData previous =
      ui2::test::ApprovedProjectFixture("name");
  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiProjectView::Build(previous, palette, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, surface, palette);
  surface.ClearDirty();

  ui2::UiProjectViewData current =
      ui2::test::ApprovedProjectFixture("render");
  current.name = "LIVE SET";
  current.tempo = "140 / C#3";
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
  ui2::UiProjectView::RenderDelta(previous, previous, scene, surface,
                                  palette);
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
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
      ui2::UiDeviceCursor::MidiDevice,
      ui2::UiDeviceCursor::MidiSync,
      ui2::UiDeviceCursor::LineOut,
      ui2::UiDeviceCursor::RemoteUi,
      ui2::UiDeviceCursor::Resampler,
      ui2::UiDeviceCursor::Volume,
      ui2::UiDeviceCursor::Brightness,
      ui2::UiDeviceCursor::Theme,
      ui2::UiDeviceCursor::Font,
      ui2::UiDeviceCursor::UpdateFirmware};
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
  REQUIRE(ui2::UiThemeView::Build(theme, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.bottomVisible);

  REQUIRE(ui2::UiFontView::Build({}, palette, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.bottomVisible);
}

TEST_CASE("UI2 Theme and Font adapters retain owned fixed-capacity text") {
  ThemeViewUi2Snapshot themeSnapshot;
  constexpr std::array themeName{'N', 'I', 'G', 'H', 'T', '\0'};
  std::copy(themeName.begin(), themeName.end(), themeSnapshot.name.begin());
  themeSnapshot.focus = ThemeViewUi2Focus::Color;
  themeSnapshot.selectedColor = 11;
  themeSnapshot.nameAction = 2;

  const ui2::UiThemeViewState themeState =
      ui2::MakeUiThemeViewState(themeSnapshot,
                                ui2::UiPowerState::BatteryHigh);
  themeSnapshot.name[0] = 'X';
  const ui2::UiThemeViewData themeData = themeState.ToViewData();
  CHECK(themeData.name == "NIGHT");
  CHECK(themeData.selectedColor == 11);
  CHECK(themeData.nameAction == 2);
  CHECK(themeData.power == ui2::UiPowerState::BatteryHigh);

  FontViewUi2Snapshot fontSnapshot;
  constexpr std::array fontName{'W', 'I', 'D', 'E', '\0'};
  std::copy(fontName.begin(), fontName.end(), fontSnapshot.font.begin());
  const ui2::UiFontViewState fontState =
      ui2::MakeUiFontViewState(fontSnapshot,
                               ui2::UiPowerState::Charging);
  fontSnapshot.font[0] = 'X';
  const ui2::UiFontViewData fontData = fontState.ToViewData();
  CHECK(fontData.font == "WIDE");
  CHECK(fontData.power == ui2::UiPowerState::Charging);
}

TEST_CASE("UI2 Theme palette synchronization is explicit and reversible") {
  ThemeViewUi2Snapshot snapshot;
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    snapshot.colors[index] =
        static_cast<std::uint32_t>(0x010203U + index * 0x070B0DU) &
        0x00FFFFFFU;
  }

  ui2::UiPalette palette;
  const ui2::Rgb888 before = palette.Get(0);
  const auto state = ui2::MakeUiThemeViewState(snapshot);
  CHECK(state.ToViewData().name == "");
  CHECK(palette.Get(0) == before);

  ui2::ApplyThemeSnapshotToPalette(snapshot, palette);
  for (std::size_t index = 0; index < snapshot.colors.size(); ++index) {
    const std::uint32_t packed = snapshot.colors[index];
    const ui2::Rgb888 expected{
        static_cast<std::uint8_t>(packed >> 16U),
        static_cast<std::uint8_t>(packed >> 8U),
        static_cast<std::uint8_t>(packed)};
    CHECK(palette.Get(static_cast<ui2::PaletteIndex>(index)) == expected);
  }

  ThemeViewUi2Snapshot roundTrip;
  ui2::CopyPaletteToThemeSnapshot(palette, roundTrip);
  CHECK(roundTrip.colors == snapshot.colors);
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
  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiFontView::Build(previous, palette, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiFontViewData current = previous;
  current.font = "CONDENSED 5X7";
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
  ui2::UiBrowserViewData data =
      ui2::test::ApprovedBrowserFixture("projects");
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
    transferredPixels +=
        static_cast<std::uint32_t>(strip.width) * strip.height;
  }
  CHECK(transferredPixels < 240U * 240U / 4U);
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
  current.loop = "PINGPONG";
  current.gain = "+3 DB";
  current.waveformRevision += 1;
  current.power = ui2::UiPowerState::BatteryLow;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 155, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSampleEditorView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSampleEditorView::RenderDelta(previous, current, currentScene,
                                       surface, palette);
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
  current.sliceCount = "08";
  current.slice = "03 / 08";
  current.start = "000119";
  current.zoom = "2X";
  current.selectedMarker = 3;
  current.waveformRevision += 1;
  current.cursorVisualOverride = true;
  current.cursorVisualRect = {7, 149, 226, 9};
  current.cursorInkVisible = false;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiSampleSlicesView::Build(current, palette, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSampleSlicesView::RenderDelta(previous, current, currentScene,
                                       surface, palette);
  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(surface.Pixels().begin(), surface.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}

TEST_CASE("UI2 Sample pages remain clean while their state is idle") {
  ui2::UiPalette palette;
  ui2::UiSampleEditorViewData editor =
      ui2::test::ApprovedSampleEditorFixture();
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

  ui2::UiSampleSlicesViewData slices =
      ui2::test::ApprovedSampleSlicesFixture();
  ui2::UiFrameScene sliceScene;
  REQUIRE(ui2::UiSampleSlicesView::Build(slices, palette, sliceScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiFrameRenderer::RenderStatic(sliceScene, surface, palette);
  surface.ClearDirty();
  ui2::UiSampleSlicesView::RenderDelta(slices, slices, sliceScene, surface,
                                       palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
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
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK_FALSE(scene.bottomVisible);
  CHECK(scene.top.Size() > 0);
  CHECK(scene.content.Size() > baseCommands);
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
  REQUIRE(ui2::UiPhraseView::Build(
              ui2::test::ApprovedPhraseFixture("fx"), palette, scene) ==
          ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiTableView::Build(
              ui2::test::ApprovedTableFixture("instrument"), palette,
              scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiInstrumentView::Build(
              ui2::test::ApprovedInstrumentFixture("opal"), palette,
              scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiDeviceView::Build(ui2::test::ApprovedDeviceFixture(),
                                   palette, scene) ==
          ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiBrowserView::Build(
              ui2::test::ApprovedBrowserFixture("projects"), palette,
              scene) == ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiGrooveView::Build(ui2::test::ApprovedGrooveFixture(),
                                   palette, scene) ==
          ui2::UiBuildStatus::Built);
  apply();
  REQUIRE(ui2::UiMixerView::Build(ui2::test::ApprovedMixerFixture(), palette,
                                  scene) == ui2::UiBuildStatus::Built);
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
                    ui2::UiDialogAction::Ok,
                    ui2::UiDialogAction::Cancel};
  dialog.actionCount = 2;
  dialog.selectedAction = 1;
  dialog.actionsFocused = true;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) ==
          ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream stream = scene.content.Stream();
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
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(std::none_of(scene.content.Commands().begin(),
                     scene.content.Commands().end(),
                     [](const ui2::UiCommand &command) {
                       return command.kind ==
                              ui2::UiCommandKind::FillCoverageRoundedRect;
                     }));
}

TEST_CASE("UI2 Dialog uses full-screen and render snapshot text") {
  ui2::UiDialogViewData full;
  full.kind = ui2::UiDialogKind::FullScreen;
  full.title = "LOW BATTERY";
  full.label = "CONNECT CHARGER";
  ui2::UiFrameScene fullScene;
  REQUIRE(ui2::UiDialogView::Apply(full, fullScene) ==
          ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream fullStream = fullScene.content.Stream();
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
  const ui2::UiCommandStream renderStream = renderScene.content.Stream();
  const std::string_view renderText(renderStream.text.data(),
                                    renderStream.text.size());
  CHECK(renderText.find("STEMS RENDERING") != std::string_view::npos);
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
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) ==
          ui2::UiBuildStatus::Built);
  CHECK(scene.topHeight == 0);
  CHECK_FALSE(scene.bottomVisible);
  CHECK(scene.top.Size() == 0);
  CHECK(scene.bottom.Size() == 0);
  CHECK(scene.content.Size() == 4);
}

TEST_CASE("UI2 retained full-screen dialog is independent of hidden base state") {
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
  REQUIRE(ui2::UiDialogView::Apply(dialog, scene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  surface.ClearDirty();
  ui2::UiDialogView::RenderDelta(dialog, dialog, scene, surface, palette);
  CHECK_FALSE(surface.DirtyTiles().Any());
}
