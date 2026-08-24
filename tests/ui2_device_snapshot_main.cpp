#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Device/UiDeviceView.h"

#include "ui2_device_fixture.h"

#include <fstream>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  const ui2::UiDeviceViewData data = ui2::test::ApprovedDeviceFixture();
  if (ui2::UiDeviceView::Build(data, palette, scene) !=
      ui2::UiBuildStatus::Built) {
    return 3;
  }
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  std::ofstream output(argv[1], std::ios::binary);
  if (!output)
    return 4;
  output << "P6\n240 240\n255\n";
  for (const ui2::PaletteIndex index : surface.Pixels()) {
    const ui2::Rgb888 color = palette.Get(index);
    output.put(static_cast<char>(color.red));
    output.put(static_cast<char>(color.green));
    output.put(static_cast<char>(color.blue));
  }
  return output ? 0 : 5;
}
