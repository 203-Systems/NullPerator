#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Views/Dialog/UiDialogView.h"
#include "UI2/Views/Song/UiSongView.h"

#include "ui2_song_fixture.h"

#include <fstream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  const std::string_view state = argv[1];
  ui2::UiDialogViewData dialog;
  if (state == "message") {
    dialog.kind = ui2::UiDialogKind::Message;
  } else if (state == "input") {
    dialog.kind = ui2::UiDialogKind::TextInput;
    dialog.title = "DIAGNOSTIC TEXT";
  } else if (state == "render") {
    dialog.kind = ui2::UiDialogKind::RenderProgress;
  } else if (state == "full") {
    dialog.kind = ui2::UiDialogKind::FullScreen;
  } else {
    return 3;
  }

  ui2::UiPalette palette;
  ui2::UiFrameScene scene;
  if (dialog.kind != ui2::UiDialogKind::FullScreen) {
    ui2::UiSongViewData song = ui2::test::ApprovedSongFixture();
    song.playing = false;
    song.power = ui2::UiPowerState::BatteryNormal;
    song.showVu = false;
    song.showBottom = false;
    if (ui2::UiSongView::Build(song, palette, scene) !=
        ui2::UiBuildStatus::Built) {
      return 4;
    }
  }
  if (ui2::UiDialogView::Apply(dialog, scene) != ui2::UiBuildStatus::Built) {
    return 5;
  }
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface surface(storage);
  ui2::UiFrameRenderer::RenderStatic(scene, surface, palette);
  std::ofstream output(argv[2], std::ios::binary);
  if (!output) return 6;
  output << "P6\n240 240\n255\n";
  for (const ui2::PaletteIndex index : surface.Pixels()) {
    const ui2::Rgb888 color = palette.Get(index);
    output.put(static_cast<char>(color.red));
    output.put(static_cast<char>(color.green));
    output.put(static_cast<char>(color.blue));
  }
  return output ? 0 : 7;
}
