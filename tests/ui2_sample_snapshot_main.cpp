#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Sample/UiSampleViews.h"

#include "ui2_sample_fixture.h"

#include <fstream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  const std::string_view state = argv[1];
  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  ui2::UiBuildStatus status = ui2::UiBuildStatus::CommandOverflow;
  if (state == "editor") {
    status = ui2::UiSampleEditorView::Build(
        ui2::test::ApprovedSampleEditorFixture(), palette, scene);
  } else if (state == "slices") {
    status = ui2::UiSampleSlicesView::Build(
        ui2::test::ApprovedSampleSlicesFixture(), palette, scene);
  } else {
    return 3;
  }
  if (status != ui2::UiBuildStatus::Built) return 4;
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  std::ofstream output(argv[2], std::ios::binary);
  if (!output) return 5;
  output << "P6\n240 240\n255\n";
  for (const ui2::PaletteIndex index : surface.Pixels()) {
    const ui2::Rgb888 color = palette.Get(index);
    output.put(static_cast<char>(color.red));
    output.put(static_cast<char>(color.green));
    output.put(static_cast<char>(color.blue));
  }
  return output ? 0 : 6;
}
