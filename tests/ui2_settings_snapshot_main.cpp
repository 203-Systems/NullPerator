#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Font/UiFontView.h"
#include "UI2/Views/Theme/UiThemeView.h"

#include <fstream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  const std::string_view state = argv[1];
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  if (state == "theme" || state == "theme-scroll") {
    ui2::UiThemeViewData data;
    if (state == "theme-scroll") {
      data.selectedColor = 18;
      const ui2::Rgb888 color = palette.Get(18U);
      data.selectedRgb = {color.red, color.green, color.blue};
      data.scrollOffset = ui2::UiThemeView::RevealCursor(0, data);
    }
    if (ui2::UiThemeView::Build(data, palette, scene) !=
        ui2::UiBuildStatus::Built) {
      return 3;
    }
  } else if (state == "font") {
    if (ui2::UiFontView::Build({}, palette, scene) !=
        ui2::UiBuildStatus::Built) {
      return 3;
    }
  } else {
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
