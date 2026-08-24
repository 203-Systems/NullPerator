#include "Application/UI2/Controllers/Ui2DeviceController.h"
#include "Application/UI2/Controllers/Ui2FontController.h"
#include "Application/UI2/Controllers/Ui2GrooveController.h"
#include "Application/UI2/Controllers/Ui2InstrumentController.h"
#include "Application/UI2/Controllers/Ui2ThemeController.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <type_traits>

namespace {

template <typename Controller>
auto Tap(Controller &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

constexpr std::uint32_t DeviceFieldBit(ui2::Ui2DeviceField field) {
  return std::uint32_t{1} << static_cast<std::uint8_t>(field);
}

} // namespace

TEST_CASE("UI2 Device cursor skips hidden rows and owns scroll position") {
  using namespace ui2;
  constexpr std::uint32_t visible =
      DeviceFieldBit(Ui2DeviceField::MidiDevice) |
      DeviceFieldBit(Ui2DeviceField::RemoteUi) |
      DeviceFieldBit(Ui2DeviceField::Resampler) |
      DeviceFieldBit(Ui2DeviceField::Brightness) |
      DeviceFieldBit(Ui2DeviceField::Theme) |
      DeviceFieldBit(Ui2DeviceField::Font);
  Ui2DeviceController controller(visible, Ui2DeviceField::MidiDevice, 3);

  CHECK(controller.SelectedField() == Ui2DeviceField::MidiDevice);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::RemoteUi);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Resampler);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Brightness);
  CHECK(controller.FirstVisibleOrdinal() == 1U);

  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Theme);
  CHECK(controller.FirstVisibleOrdinal() == 2U);
  const auto browse = Tap(controller, TrackerAction::Enter);
  CHECK(browse.type == Ui2DeviceCommandType::BrowseTheme);
  CHECK(browse.field == Ui2DeviceField::Theme);

  for (int repeat = 0; repeat < 10; ++repeat)
    Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Font);
}

TEST_CASE("UI2 Device selectors preserve each field's wrap contract") {
  using namespace ui2;
  Ui2DeviceController controller;
  controller.SetSelector(Ui2DeviceField::MidiDevice, {4U, 0U, false});
  controller.SetSelector(Ui2DeviceField::RemoteUi, {2U, 0U, true});

  CHECK(controller.Bottom().kind == Ui2DeviceBottomKind::Selector);
  CHECK(controller.Bottom().count == 4U);
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  for (std::uint16_t expected = 1; expected <= 3; ++expected) {
    const auto command = Tap(controller, TrackerAction::Right);
    REQUIRE(command.HasValue());
    CHECK(command.type == Ui2DeviceCommandType::SetSelector);
    CHECK(command.field == Ui2DeviceField::MidiDevice);
    CHECK(command.value == expected);
  }
  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
  CHECK(controller.Selector(Ui2DeviceField::MidiDevice).current == 3U);

  Tap(controller, TrackerAction::Down); // MIDI sync
  Tap(controller, TrackerAction::Down); // Remote UI
  CHECK(controller.SelectedField() == Ui2DeviceField::RemoteUi);
  const auto wrapped = Tap(controller, TrackerAction::Left);
  REQUIRE(wrapped.HasValue());
  CHECK(wrapped.value == 1U);
  CHECK(controller.Bottom().wrap);
}

TEST_CASE("UI2 Theme owns NAME actions and all nineteen palette rows") {
  using namespace ui2;
  Ui2ThemeController controller(-1, Ui2ThemeNameAction::New, 6);
  constexpr Ui2ThemeCommandType actions[] = {
      Ui2ThemeCommandType::NewTheme, Ui2ThemeCommandType::LoadTheme,
      Ui2ThemeCommandType::SaveTheme, Ui2ThemeCommandType::RenameTheme};

  for (std::uint8_t index = 0; index < 4U; ++index) {
    REQUIRE(controller.NameSelected());
    const auto bottom = controller.Bottom();
    CHECK(bottom.kind == Ui2ThemeBottomKind::NameActions);
    CHECK(bottom.selectedIndex == index);
    CHECK(bottom.optionCount == 4U);
    CHECK(Tap(controller, TrackerAction::Enter).type == actions[index]);
    Tap(controller, TrackerAction::Right);
  }
  CHECK(controller.NameAction() == Ui2ThemeNameAction::New);
  Tap(controller, TrackerAction::Left);
  CHECK(controller.NameAction() == Ui2ThemeNameAction::Rename);

  for (std::uint8_t color = 0; color < Ui2ThemeController::ColorCount;
       ++color) {
    Tap(controller, TrackerAction::Down);
    CHECK(controller.SelectedColor() == static_cast<std::int8_t>(color));
  }
  CHECK(controller.FirstVisibleOrdinal() == 14U);
  CHECK(controller.Bottom().kind == Ui2ThemeBottomKind::Hidden);
  const auto color = Tap(controller, TrackerAction::Enter);
  CHECK(color.type == Ui2ThemeCommandType::ActivateColor);
  CHECK(color.color == 18);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedColor() == 18);
}

TEST_CASE("UI2 Font has one BROWSE content action and no bottom bar") {
  ui2::Ui2FontController controller;
  CHECK(controller.BrowseSelected());
  CHECK_FALSE(controller.BottomVisible());
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  CHECK(Tap(controller, TrackerAction::Enter).type ==
        ui2::Ui2FontCommandType::BrowseFont);
}

TEST_CASE("UI2 Instrument name actions and type selector are independent") {
  using namespace ui2;
  Ui2InstrumentController controller;
  constexpr Ui2InstrumentCommandType actions[] = {
      Ui2InstrumentCommandType::LoadInstrument,
      Ui2InstrumentCommandType::SaveInstrument,
      Ui2InstrumentCommandType::RenameInstrument};

  for (std::uint8_t index = 0; index < 3U; ++index) {
    CHECK(controller.Cursor().kind == Ui2InstrumentCursorKind::Name);
    CHECK(controller.Bottom().kind == Ui2InstrumentBottomKind::NameActions);
    CHECK(controller.Bottom().selectedIndex == index);
    CHECK(Tap(controller, TrackerAction::Enter).type == actions[index]);
    Tap(controller, TrackerAction::Right);
  }

  Tap(controller, TrackerAction::Down);
  CHECK(controller.Cursor().kind == Ui2InstrumentCursorKind::Type);
  controller.SetTypeSelector({5U, 4U, true});
  const auto wrapped = Tap(controller, TrackerAction::Right);
  REQUIRE(wrapped.HasValue());
  CHECK(wrapped.type == Ui2InstrumentCommandType::SetType);
  CHECK(wrapped.value == 0);
  CHECK(controller.Bottom().kind == Ui2InstrumentBottomKind::TypeSelector);
  CHECK(controller.Bottom().wrap);
}

TEST_CASE("UI2 Instrument scrolls a fixed list including both OPAL columns") {
  using namespace ui2;
  Ui2InstrumentController controller(0, 0, 12, 6, {}, {5U, 0U, true}, 5);
  for (int repeat = 0; repeat < 16; ++repeat)
    Tap(controller, TrackerAction::Down);
  CHECK(controller.FirstVisibleOrdinal() > 0U);
  CHECK(controller.Cursor().kind == Ui2InstrumentCursorKind::Operator1);
  CHECK(controller.Cursor().index == 2U);

  Tap(controller, TrackerAction::Right);
  CHECK(controller.Cursor().kind == Ui2InstrumentCursorKind::Operator2);
  CHECK(controller.Cursor().index == 2U);
  Tap(controller, TrackerAction::Left);
  CHECK(controller.Cursor().kind == Ui2InstrumentCursorKind::Operator1);
}

TEST_CASE("UI2 Instrument Edit owns top number and bottom track dual focus") {
  using namespace ui2;
  Ui2InstrumentController controller(0, 2, 4, 0);
  CHECK_FALSE(controller.NumberFocus());
  controller.Handle(TrackerAction::Edit, true);
  CHECK(controller.NumberFocus());
  CHECK(controller.TrackFocus());
  CHECK(controller.Bottom().kind == Ui2InstrumentBottomKind::TrackNotes);

  const auto track = controller.Handle(TrackerAction::Right, true);
  REQUIRE(track.HasValue());
  CHECK(track.type == Ui2InstrumentCommandType::SelectTrack);
  CHECK(track.value == 3);
  CHECK(controller.SelectedTrack() == 3U);
  controller.Handle(TrackerAction::Right, false);

  const auto number = controller.Handle(TrackerAction::Up, true);
  REQUIRE(number.HasValue());
  CHECK(number.type == Ui2InstrumentCommandType::SelectNumber);
  CHECK(number.value == 38);
  CHECK(controller.Number() == 38U);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Edit, false);
  CHECK_FALSE(controller.NumberFocus());
}

TEST_CASE("UI2 Instrument Enter emits typed field edits and one commit") {
  using namespace ui2;
  Ui2InstrumentController controller(
      0, 0, 8, 0, {Ui2InstrumentCursorKind::Field, 3});
  const auto activate = controller.Handle(TrackerAction::Enter, true);
  REQUIRE(activate.HasValue());
  CHECK(activate.type == Ui2InstrumentCommandType::ActivateField);
  CHECK(activate.cursor.kind == Ui2InstrumentCursorKind::Field);
  CHECK(activate.cursor.index == 3U);

  const auto adjust = controller.Handle(TrackerAction::Up, true);
  REQUIRE(adjust.HasValue());
  CHECK(adjust.type == Ui2InstrumentCommandType::AdjustField);
  CHECK(adjust.direction == Ui2InstrumentValueDirection::Up);
  CHECK(adjust.value == 1);
  controller.Handle(TrackerAction::Up, false);
  const auto commit = controller.Handle(TrackerAction::Enter, false);
  CHECK(commit.type == Ui2InstrumentCommandType::CommitValueEdits);
  CHECK_FALSE(controller.Handle(TrackerAction::Enter, false).HasValue());
}

TEST_CASE("UI2 Groove owns sixteen wrapping rows and never a bottom bar") {
  using namespace ui2;
  Ui2GrooveController controller(0, 0);
  CHECK_FALSE(controller.BottomVisible());
  Tap(controller, TrackerAction::Up);
  CHECK(controller.Row() == 15U);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.Row() == 0U);

  CHECK(Tap(controller, TrackerAction::Enter).type ==
        Ui2GrooveCommandType::InitializeStep);
  controller.Handle(TrackerAction::Enter, true);
  const auto adjust = controller.Handle(TrackerAction::Up, true);
  REQUIRE(adjust.HasValue());
  CHECK(adjust.type == Ui2GrooveCommandType::AdjustStep);
  CHECK(adjust.value == 1);
  CHECK(adjust.synchronized);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Enter, false);
}

TEST_CASE("UI2 Groove Edit clears cells and wraps the groove number") {
  using namespace ui2;
  Ui2GrooveController controller(0, 4);
  controller.Handle(TrackerAction::Edit, true);
  const auto clear = controller.Handle(TrackerAction::Enter, true);
  REQUIRE(clear.HasValue());
  CHECK(clear.type == Ui2GrooveCommandType::ClearStep);
  CHECK(clear.row == 4U);
  controller.Handle(TrackerAction::Enter, false);

  const auto number = controller.Handle(TrackerAction::Left, true);
  REQUIRE(number.HasValue());
  CHECK(number.type == Ui2GrooveCommandType::SelectNumber);
  CHECK(number.value == 31);
  CHECK(controller.Number() == 31U);
  controller.Handle(TrackerAction::Left, false);
  controller.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 settings controllers keep fixed-capacity trivial state") {
  using namespace ui2;
  CHECK(std::is_trivially_copyable_v<Ui2DeviceController>);
  CHECK(std::is_trivially_copyable_v<Ui2ThemeController>);
  CHECK(std::is_trivially_copyable_v<Ui2FontController>);
  CHECK(std::is_trivially_copyable_v<Ui2InstrumentController>);
  CHECK(std::is_trivially_copyable_v<Ui2GrooveController>);
  CHECK(sizeof(Ui2DeviceController) <= 80U);
  CHECK(sizeof(Ui2ThemeController) <= 16U);
  CHECK(sizeof(Ui2FontController) <= 4U);
  CHECK(sizeof(Ui2InstrumentController) <= 40U);
  CHECK(sizeof(Ui2GrooveController) <= 8U);
}
