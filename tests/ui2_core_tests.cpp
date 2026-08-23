#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Animation/UiTransitionTimeline.h"
#include "UI2/Chrome/UiBarResolver.h"
#include "UI2/Chrome/UiChromeRenderer.h"
#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Render/UiVuGradient.h"
#include "UI2/Scene/UiCommandList.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/UiEngine.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Song/UiSongView.h"
#include "Adapters/wasm/gui/WasmUiPresenter.h"
#include "Application/UI2/Ui2ApplicationRuntime.h"

#include "ui2_song_fixture.h"

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
