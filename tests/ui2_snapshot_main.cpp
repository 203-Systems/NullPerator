#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Song/UiSongView.h"

#include "ui2_song_fixture.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  if (argc != 2 && argc != 7) {
    std::cerr << "usage: ui2_snapshot_tool <output.ppm> "
                 "[cursor-x cursor-y edit-track edit-row ink-visible]\n";
    return 2;
  }
  ui2::UiSongViewData data = ui2::test::ApprovedSongFixture();
  if (argc == 7) {
    data.cursorVisualOverride = true;
    data.cursorVisualRect = {
        static_cast<std::int16_t>(std::stoi(argv[2])),
        static_cast<std::int16_t>(std::stoi(argv[3])), 15, 9};
    data.editTrack = static_cast<std::uint8_t>(
        std::clamp(std::stoi(argv[4]), 0, 7));
    data.editRow = static_cast<std::uint8_t>(
        std::clamp(std::stoi(argv[5]), 0, 15));
    data.cursorInkVisible = std::stoi(argv[6]) != 0;
  }
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  if (ui2::UiSongView::Build(data, palette, scene) !=
      ui2::UiBuildStatus::Built) {
    std::cerr << "failed to build Song scene\n";
    return 3;
  }
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);

  std::ofstream output(argv[1], std::ios::binary);
  if (!output) return 4;
  output << "P6\n240 240\n255\n";
  for (const ui2::PaletteIndex index : surface.Pixels()) {
    const ui2::Rgb888 color = palette.Get(index);
    output.put(static_cast<char>(color.red));
    output.put(static_cast<char>(color.green));
    output.put(static_cast<char>(color.blue));
  }
  return output ? 0 : 5;
}
