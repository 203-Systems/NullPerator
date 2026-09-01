#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"
#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"
#include "UI2/Views/Dialog/UiDialogView.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

const ui2::UiCommand *FindTextCommand(const ui2::UiCommandStream &stream,
                                      std::string_view text) {
  for (const ui2::UiCommand &command : stream.commands) {
    if (command.kind != ui2::UiCommandKind::Text ||
        command.auxiliaryColor != text.size())
      continue;
    const std::size_t begin = command.payload;
    if (begin + text.size() <= stream.text.size() &&
        std::equal(text.begin(), text.end(), stream.text.begin() + begin))
      return &command;
  }
  return nullptr;
}

} // namespace

TEST_CASE("Rename dialog snapshot is fixed-capacity and owns its projection") {
  std::string source = "ABCDEFGHIJKLMNOPQRSTUV";
  Ui2DialogSnapshot snapshot;
  snapshot.kind = ui2::UiDialogKind::Rename;
  snapshot.SetTitle("RENAME");
  snapshot.SetLabel("NAME");
  snapshot.SetValue(source);
  snapshot.PushAction(ui2::UiDialogAction::Cancel);
  snapshot.PushAction(ui2::UiDialogAction::Random);
  snapshot.PushAction(ui2::UiDialogAction::Save);
  snapshot.SetSelectedAction(2, true);
  snapshot.SetRenameFocus(ui2::UiDialogFocus::Keyboard, 17U);

  source.assign(source.size(), 'X');
  const ui2::UiDialogViewData data = snapshot.ToViewData();
  CHECK(data.kind == ui2::UiDialogKind::Rename);
  CHECK(data.value == "ABCDEFGHIJKLMNOPQRST");
  CHECK(data.actionCount == 3U);
  CHECK(data.selectedAction == 2U);
  CHECK(data.focus == ui2::UiDialogFocus::Keyboard);
  CHECK(data.selectedKey == 17U);
  CHECK_FALSE(data.actionsFocused);
  CHECK(data.uppercase);
  CHECK(std::is_trivially_copyable_v<Ui2DialogSnapshot>);
  CHECK(sizeof(Ui2DialogSnapshot) <= 128U);
}

TEST_CASE("Rename keyboard has visually distinct lower-case glyphs") {
  CHECK(ui2::UiFont5x7::Glyph('a') != ui2::UiFont5x7::Glyph('A'));
  CHECK(ui2::UiFont5x7::Glyph('g') != ui2::UiFont5x7::Glyph('G'));
  CHECK(ui2::UiFont5x7::Glyph('p') != ui2::UiFont5x7::Glyph('P'));
  CHECK(ui2::UiFont5x7::Glyph('p')[1] == 0b10110);
}

TEST_CASE("Rename full page and all cursor targets stay in 240x240") {
  ui2::UiDialogViewData data;
  data.kind = ui2::UiDialogKind::Rename;
  data.label = "NAME";
  data.value = "ONECYCAC";
  data.actions = {ui2::UiDialogAction::Cancel,
                  ui2::UiDialogAction::Random,
                  ui2::UiDialogAction::Save,
                  ui2::UiDialogAction::Cancel};
  data.actionCount = 3U;

  CHECK(ui2::UiDialogView::DamageRect(data.kind) ==
        ui2::RectI16::Screen());
  data.focus = ui2::UiDialogFocus::Input;
  CHECK(ui2::UiDialogView::CursorTargetRect(data) ==
        ui2::RectI16{9, 53, 222, 15});

  data.focus = ui2::UiDialogFocus::Keyboard;
  data.selectedKey = 0U;
  CHECK(ui2::UiDialogView::CursorTargetRect(data) ==
        ui2::RectI16{8, 81, 13, 11});
  data.selectedKey = 40U;
  CHECK(ui2::UiDialogView::CursorTargetRect(data) ==
        ui2::RectI16{196, 168, 35, 13});

  data.focus = ui2::UiDialogFocus::Actions;
  data.selectedAction = 0U;
  CHECK(ui2::UiDialogView::CursorTargetRect(data).Empty());
  data.selectedAction = 1U;
  CHECK(ui2::UiDialogView::CursorTargetRect(data).Empty());
  data.selectedAction = 2U;
  CHECK(ui2::UiDialogView::CursorTargetRect(data).Empty());

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(data, scene) == ui2::UiBuildStatus::Built);
  CHECK(scene.content.Size() == 0U);
  for (const ui2::UiCommand &command : scene.overlay.Commands()) {
    if (command.bounds.Empty())
      continue;
    CHECK(command.bounds.x >= 0);
    CHECK(command.bounds.y >= 0);
    CHECK(command.bounds.Right() <= 240);
    CHECK(command.bounds.Bottom() <= 240);
  }
}

TEST_CASE("Rename page exposes pixel icons and three actions") {
  ui2::UiDialogViewData data;
  data.kind = ui2::UiDialogKind::Rename;
  data.label = "NAME";
  data.value = "ONECYCAC";
  data.actions = {ui2::UiDialogAction::Cancel,
                  ui2::UiDialogAction::Random,
                  ui2::UiDialogAction::Save,
                  ui2::UiDialogAction::Cancel};
  data.actionCount = 3U;
  data.selectedAction = 2U;
  data.focus = ui2::UiDialogFocus::Actions;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(data, scene) == ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream stream = scene.overlay.Stream();
  const std::string_view text(stream.text.data(), stream.text.size());
  CHECK(text.find("RENAME") != std::string_view::npos);
  CHECK(text.find("ONECYCAC") != std::string_view::npos);
  CHECK(text.find("1234567890") != std::string_view::npos);
  CHECK(text.find("QWERTYUIOP") != std::string_view::npos);
  CHECK(text.find("-") != std::string_view::npos);
  CHECK(text.find(".") != std::string_view::npos);
  CHECK(text.find("CANCEL") != std::string_view::npos);
  CHECK(text.find("SAVE") != std::string_view::npos);
  CHECK(text.find("RANDOM") != std::string_view::npos);
  CHECK(std::count_if(stream.commands.begin(), stream.commands.end(),
                      [](const ui2::UiCommand &command) {
                        return command.kind ==
                               ui2::UiCommandKind::FillCoverageRoundedRect;
                      }) == 0);
  CHECK(std::count_if(stream.commands.begin(), stream.commands.end(),
                      [](const ui2::UiCommand &command) {
                        return command.kind == ui2::UiCommandKind::PixelMask;
                      }) == 4);

  const ui2::UiCommand *save = FindTextCommand(stream, "SAVE");
  REQUIRE(save != nullptr);
  CHECK(save->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextColored));

  data.focus = ui2::UiDialogFocus::Input;
  ui2::UiFrameScene inputScene;
  REQUIRE(ui2::UiDialogView::Apply(data, inputScene) ==
          ui2::UiBuildStatus::Built);
  save = FindTextCommand(inputScene.overlay.Stream(), "SAVE");
  REQUIRE(save != nullptr);
  CHECK(save->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextDim));
}

TEST_CASE("Rename keyboard case bypasses the global label case mode") {
  ui2::UiDialogViewData data;
  data.kind = ui2::UiDialogKind::Rename;
  data.uppercase = false;

  ui2::UiFrameScene scene;
  REQUIRE(ui2::UiDialogView::Apply(data, scene) == ui2::UiBuildStatus::Built);
  const ui2::UiCommandStream lower = scene.overlay.Stream();
  const ui2::UiCommand *q = FindTextCommand(lower, "q");
  const auto lowerShift = std::find_if(
      lower.commands.begin(), lower.commands.end(),
      [](const ui2::UiCommand &command) {
        return command.kind == ui2::UiCommandKind::PixelMask &&
               command.bounds == ui2::RectI16{21, 169, 11, 11};
      });
  REQUIRE(q != nullptr);
  REQUIRE(lowerShift != lower.commands.end());
  CHECK((q->parameter & 0x80U) != 0U);
  CHECK(lowerShift->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextDim));

  data.uppercase = true;
  REQUIRE(ui2::UiDialogView::Apply(data, scene) == ui2::UiBuildStatus::Built);
  const ui2::UiCommand *upperQ =
      FindTextCommand(scene.overlay.Stream(), "Q");
  REQUIRE(upperQ != nullptr);
  CHECK((upperQ->parameter & 0x80U) != 0U);
  const ui2::UiCommandStream upper = scene.overlay.Stream();
  const auto upperShift = std::find_if(
      upper.commands.begin(), upper.commands.end(),
      [](const ui2::UiCommand &command) {
        return command.kind == ui2::UiCommandKind::PixelMask &&
               command.bounds == ui2::RectI16{21, 169, 11, 11};
      });
  REQUIRE(upperShift != upper.commands.end());
  CHECK(upperShift->color ==
        static_cast<ui2::PaletteIndex>(ui2::UiColorToken::TextDim));
}

TEST_CASE("Rename special-key icons preserve approved pixel geometry") {
  ui2::UiPalette palette;
  ui2::UiDialogViewData data;
  data.kind = ui2::UiDialogKind::Rename;
  data.uppercase = false;

  ui2::UiFrameScene lowerScene;
  REQUIRE(ui2::UiDialogView::Apply(data, lowerScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage lowerStorage;
  ui2::UiIndexedSurface lower(lowerStorage);
  ui2::UiFrameRenderer::RenderStatic(lowerScene, lower, palette);

  const ui2::PaletteIndex background =
      palette.Index(ui2::UiColorToken::SurfaceBackground);
  const ui2::PaletteIndex dim = palette.Index(ui2::UiColorToken::TextDim);
  CHECK(lower.Pixel(26, 169) == dim);        // Shift tip.
  CHECK(lower.Pixel(26, 172) == background); // Hollow Shift body.
  CHECK(lower.Pixel(110, 173) == dim);       // Space left wall.
  CHECK(lower.Pixel(111, 173) == background);
  CHECK(lower.Pixel(120, 177) == dim); // Space floor.
  CHECK(lower.Pixel(209, 169) == dim); // Backspace top edge.
  CHECK(lower.Pixel(210, 170) == background);
  CHECK(lower.Pixel(214, 171) == dim); // Five-by-five X corner.

  data.uppercase = true;
  ui2::UiFrameScene upperScene;
  REQUIRE(ui2::UiDialogView::Apply(data, upperScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage upperStorage;
  ui2::UiIndexedSurface upper(upperStorage);
  ui2::UiFrameRenderer::RenderStatic(upperScene, upper, palette);
  CHECK(upper.Pixel(26, 172) ==
        palette.Index(ui2::UiColorToken::TextDim));
}

TEST_CASE("Rename cursor-only changes are pixel-identical to full redraw") {
  ui2::UiPalette palette;
  ui2::UiDialogViewData previous;
  previous.kind = ui2::UiDialogKind::Rename;
  previous.label = "NAME";
  previous.value = "ONECYCAC";
  previous.actions = {ui2::UiDialogAction::Cancel,
                      ui2::UiDialogAction::Random,
                      ui2::UiDialogAction::Save,
                      ui2::UiDialogAction::Cancel};
  previous.actionCount = 3U;
  previous.focus = ui2::UiDialogFocus::Input;

  ui2::UiFrameScene previousScene;
  REQUIRE(ui2::UiDialogView::Apply(previous, previousScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiSurfaceStorage storage;
  ui2::UiIndexedSurface actual(storage);
  ui2::UiFrameRenderer::RenderStatic(previousScene, actual, palette);
  actual.ClearDirty();

  ui2::UiDialogViewData current = previous;
  current.focus = ui2::UiDialogFocus::Keyboard;
  current.selectedKey = 12U;
  ui2::UiFrameScene currentScene;
  REQUIRE(ui2::UiDialogView::Apply(current, currentScene) ==
          ui2::UiBuildStatus::Built);
  ui2::UiDialogView::RenderDelta(previous, current, currentScene, actual,
                                 palette);

  ui2::UiSurfaceStorage expectedStorage;
  ui2::UiIndexedSurface expected(expectedStorage);
  ui2::UiFrameRenderer::RenderStatic(currentScene, expected, palette);
  CHECK(std::equal(actual.Pixels().begin(), actual.Pixels().end(),
                   expected.Pixels().begin(), expected.Pixels().end()));
}
