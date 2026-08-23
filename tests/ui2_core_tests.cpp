#include "UI2/Animation/UiMotionTrack.h"
#include "UI2/Render/IUiPresenter.h"
#include "UI2/Render/UiDirtyTiles.h"
#include "UI2/Render/UiIndexedSurface.h"
#include "UI2/Scene/UiCommandList.h"
#include "UI2/Theme/UiPalette.h"
#include "UI2/UiEngine.h"

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

TEST_CASE("UI2 fixed-point easing is nonlinear and lands exactly") {
  ui2::UiMotionTrack track;
  track.Start(0, 240, 1'000, 180);
  CHECK(track.Sample(1'000) == 0);
  CHECK(track.Sample(1'045) > 60); // Ease-out is ahead of linear at 25%.
  CHECK(track.Sample(1'090) > 120);
  CHECK(track.Sample(1'180) == 240);
  CHECK_FALSE(track.Active(1'180));
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
