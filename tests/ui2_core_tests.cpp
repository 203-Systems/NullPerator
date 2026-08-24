#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Animation/UiTransitionTimeline.h"
#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Render/UiRgb565Presenter.h"
#include "UI2/Render/UiVuGradient.h"
#include "UI2/Scene/UiCommandList.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/UiEngine.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Chain/UiChainView.h"
#include "UI2/Views/Device/UiDeviceView.h"
#include "UI2/Views/Groove/UiGrooveView.h"
#include "UI2/Views/Song/UiSongView.h"
#include "UI2/Views/Phrase/UiPhraseView.h"
#include "UI2/Views/Instrument/UiInstrumentView.h"
#include "UI2/Views/Mixer/UiMixerView.h"
#include "UI2/Views/Project/UiProjectView.h"
#include "UI2/Views/Table/UiTableView.h"
#include "Adapters/wasm/gui/WasmUiPresenter.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"

#include "ui2_song_fixture.h"
#include "ui2_chain_fixture.h"
#include "ui2_device_fixture.h"
#include "ui2_groove_fixture.h"
#include "ui2_phrase_fixture.h"
#include "ui2_instrument_fixture.h"
#include "ui2_mixer_fixture.h"
#include "ui2_project_fixture.h"
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

TEST_CASE("UI2 semantic palette reproduces approved coverage composites") {
  ui2::UiPalette palette;
  CHECK(palette.Get(palette.Index(ui2::UiColorToken::SurfaceField)) ==
        ui2::Rgb888{0x03, 0x07, 0x07});
  CHECK(palette.Get(palette.CoverageIndex(
            ui2::UiCoverage::Playback,
            palette.Index(ui2::UiColorToken::SurfaceField))) ==
        ui2::Rgb888{0x2D, 0x65, 0x45});
  CHECK(palette.Get(palette.CoverageIndex(
            ui2::UiCoverage::Playback,
            palette.Index(ui2::UiColorToken::CursorRow))) ==
        ui2::Rgb888{0x38, 0x6E, 0x50});
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
      .enterHeldTracks = &tracks,
      .enterHeldNumber = true,
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
        palette.Index(ui2::UiColorToken::SurfaceBarDeep));
  CHECK(surface.Pixel(5, 34) ==
        palette.Index(ui2::UiColorToken::SurfaceField));
  CHECK(surface.Pixel(5, 127) ==
        palette.Index(ui2::UiColorToken::CursorRow));
  CHECK(surface.Pixel(219, 47) ==
        palette.Index(ui2::UiColorToken::VuTrack));
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
  surface.Clear(palette.Index(ui2::UiColorToken::SurfaceField));
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
  const auto field = palette.Index(ui2::UiColorToken::SurfaceField);
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
        ui2::RectI16{131, 76, 15, 9});
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
        ui2::RectI16{68, 211, 15, 9});
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

TEST_CASE("UI2 Mixer stereo meters use separate damage columns") {
  CHECK(ui2::UiMixerView::MeterDamageRect(0, 0) ==
        ui2::RectI16{7, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterDamageRect(0, 1) ==
        ui2::RectI16{17, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterDamageRect(8, 0) ==
        ui2::RectI16{209, 46, 7, 153});
  CHECK(ui2::UiMixerView::MeterDamageRect(8, 1) ==
        ui2::RectI16{219, 46, 7, 153});
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
  CHECK(ui2::UiChainView::VuDamageRect(0) ==
        ui2::RectI16{219, 50, 7, 148});
  CHECK(ui2::UiChainView::VuDamageRect(1) ==
        ui2::RectI16{228, 50, 7, 148});
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
  CHECK(transferredPixels < 4'000);
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
