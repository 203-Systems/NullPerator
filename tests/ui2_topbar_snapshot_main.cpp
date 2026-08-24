#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Song/UiSongView.h"

#include "ui2_song_fixture.h"

#include <fstream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  const std::string_view state = argv[1];
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  if (state == "playing") {
    data.power = ui2::UiPowerState::Playing;
    data.playing = true;
  } else if (state == "low") {
    data.power = ui2::UiPowerState::BatteryLow;
    data.playing = false;
  } else if (state == "high") {
    data.power = ui2::UiPowerState::BatteryHigh;
    data.playing = false;
  } else if (state == "charging") {
    data.power = ui2::UiPowerState::Charging;
    data.playing = false;
  } else if (state == "nav") {
    data.power = ui2::UiPowerState::Navigation;
    data.playing = false;
  } else {
    return 3;
  }

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  if (ui2::UiSongView::Build(data, palette, scene) !=
      ui2::UiBuildStatus::Built) {
    return 4;
  }
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  std::ofstream output(argv[2], std::ios::binary);
  if (!output)
    return 5;
  output << "P6\n240 240\n255\n";
  for (const ui2::PaletteIndex index : surface.Pixels()) {
    const ui2::Rgb888 color = palette.Get(index);
    output.put(static_cast<char>(color.red));
    output.put(static_cast<char>(color.green));
    output.put(static_cast<char>(color.blue));
  }
  return output ? 0 : 6;
}
