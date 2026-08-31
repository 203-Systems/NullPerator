#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

// TinyXML's embedded adapter remaps stdio names. Keep the host standard
// library above production persistence headers so libc++ always sees FILE.
#include "Application/UI2/Controllers/Ui2DeviceController.h"
#include "Application/UI2/Controllers/Ui2FontController.h"
#include "Application/UI2/Controllers/Ui2GrooveController.h"
#include "Application/UI2/Controllers/Ui2InstrumentController.h"
#include "Application/UI2/Controllers/Ui2InstrumentBrowserController.h"
#include "Application/UI2/Controllers/Ui2MixerController.h"
#include "Application/UI2/Controllers/Ui2RenameController.h"
#include "Application/UI2/Controllers/Ui2RecordController.h"
#include "Application/UI2/Controllers/Ui2ThemeController.h"
#include "Application/UI2/Controllers/Ui2SettingsBrowserController.h"
#include "Application/UI2/Ui2BrightnessMapping.h"
#include "Application/UI2/Ui2ConfigSaveState.h"
#include "Application/UI2/Ui2TransportPolicy.h"
#include "Application/UI2/Workflows/Ui2FontWorkflow.h"
#include "Application/UI2/Workflows/Ui2GrooveWorkflow.h"
#include "Application/UI2/Workflows/Ui2ThemeWorkflow.h"

namespace {

template <typename Controller>
auto Tap(Controller &controller, TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

enum class FakePlayMode : std::uint8_t { Song, Chain };

struct FakeSongTransport {
  void OnStartButton(FakePlayMode origin, unsigned int from,
                     bool startFromPrevious, unsigned char chainPosition) {
    ++buttonCalls;
    lastOrigin = origin;
    lastFrom = from;
    lastStartFromPrevious = startFromPrevious;
    lastChainPosition = chainPosition;
    if (running) {
      ++stopCalls;
      running = false;
    } else {
      ++startCalls;
      running = true;
    }
  }

  FakePlayMode lastOrigin = FakePlayMode::Chain;
  unsigned int lastFrom = 0U;
  unsigned char lastChainPosition = 0U;
  int buttonCalls = 0;
  int startCalls = 0;
  int stopCalls = 0;
  std::uint8_t muteMask = 0xA5U;
  std::uint8_t soloMask = 0x24U;
  std::uint32_t vuGeneration = 73U;
  bool lastStartFromPrevious = true;
  bool running = false;
};

class CountingFontVariable final : public Variable {
public:
  CountingFontVariable()
      : Variable(FourCC::VarUIFont, kNames, 3, 0) {}

  void Reset() override {
    ++resetCalls;
    Variable::Reset();
  }

  int resetCalls = 0;

private:
  static constexpr const char *kNames[3]{"Regular", "Bold", "Wide"};
};

constexpr std::uint32_t DeviceFieldBit(ui2::Ui2DeviceField field) {
  return std::uint32_t{1} << static_cast<std::uint8_t>(field);
}

} // namespace

TEST_CASE("UI2 config save state retains failed writes for retry") {
  ui2::Ui2ConfigSaveState state;
  int attempts = 0;
  state.MarkDirty();
  CHECK_FALSE(state.Flush([&attempts]() {
    ++attempts;
    return false;
  }));
  CHECK(state.Dirty());
  CHECK(state.Flush([&attempts]() {
    ++attempts;
    return true;
  }));
  CHECK_FALSE(state.Dirty());
  CHECK(state.Flush([&attempts]() {
    ++attempts;
    return false;
  }));
  CHECK(attempts == 2);
}

TEST_CASE("UI2 input repeats only directional movement") {
  using namespace ui2;
  constexpr std::uint16_t up = TrackerActionBit(TrackerAction::Up);
  constexpr std::uint16_t edit = TrackerActionBit(TrackerAction::Edit);
  constexpr std::uint16_t play = TrackerActionBit(TrackerAction::Play);

  CHECK(Ui2AcceptInputEvent(TrackerAction::Up, true, 0U));
  CHECK(Ui2AcceptInputEvent(TrackerAction::Up, true, up));
  CHECK(Ui2AcceptInputEvent(TrackerAction::Edit, true, 0U));
  CHECK_FALSE(Ui2AcceptInputEvent(TrackerAction::Edit, true, edit));
  CHECK_FALSE(Ui2AcceptInputEvent(TrackerAction::Play, true, play));
  CHECK(Ui2AcceptInputEvent(TrackerAction::Edit, false, edit));
}

TEST_CASE("UI2 brightness percentage preserves a visible hardware floor") {
  CHECK(ui2::Ui2BrightnessRawFromPercent(0U) == 0x0FU);
  CHECK(ui2::Ui2BrightnessRawFromPercent(100U) == 0xFFU);
  CHECK(ui2::Ui2BrightnessRawFromPercent(200U) == 0xFFU);
  CHECK(ui2::Ui2BrightnessPercentFromRaw(0) == 0U);
  CHECK(ui2::Ui2BrightnessPercentFromRaw(0x0F) == 0U);
  CHECK(ui2::Ui2BrightnessPercentFromRaw(0xFF) == 100U);
  CHECK(ui2::Ui2BrightnessPercentFromRaw(
            ui2::Ui2BrightnessRawFromPercent(50U)) == 50U);
}

TEST_CASE("UI2 instrument browser leaves return navigation to Shift Left") {
  using namespace ui2;
  Ui2InstrumentBrowserController controller;
  const Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(std::string(snapshot.title.data()) == "IMPORT");
  CHECK(std::string(snapshot.meta.data()).empty());
  CHECK(snapshot.totalItemCount == 0U);
  CHECK_FALSE(snapshot.hasSelection);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2InstrumentBrowserCommandType::None);
  CHECK(Tap(controller, TrackerAction::Option).type ==
        Ui2InstrumentBrowserCommandType::None);
  CHECK(std::is_trivially_copyable_v<Ui2InstrumentBrowserCommand>);

  controller.SetError("INSTRUMENT LOAD FAILED");
  CHECK(std::string(controller.Snapshot().footer.data()) ==
        "INSTRUMENT LOAD FAILED");
}

TEST_CASE("UI2 settings browser does not fabricate font entries") {
  using namespace ui2;
  Ui2SettingsBrowserController controller;
  CHECK_FALSE(controller.Active());
  CHECK(controller.Mode() == Ui2SettingsBrowserMode::None);
  const Ui2BrowserSnapshot snapshot = controller.Snapshot();
  CHECK(std::string(snapshot.title.data()) == "THEMES");
  CHECK(std::string(snapshot.meta.data()).empty());
  CHECK(snapshot.totalItemCount == 0U);
  CHECK_FALSE(snapshot.hasSelection);
}

TEST_CASE("UI2 sample instrument fields emit editor and slices activation commands") {
  using namespace ui2;
  Ui2InstrumentController controller(0U, 0U, 11U, 0U);
  Tap(controller, TrackerAction::Down); // TYPE
  Tap(controller, TrackerAction::Down); // SAMPLE
  CHECK(controller.Cursor().kind == Ui2InstrumentCursorKind::Field);
  CHECK(controller.Cursor().index == 0U);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2InstrumentCommandType::ActivateField);
  Tap(controller, TrackerAction::Down); // SLICES
  CHECK(controller.Cursor().index == 1U);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2InstrumentCommandType::ActivateField);
}

TEST_CASE("UI2 Device cursor skips hidden rows and owns scroll position") {
  using namespace ui2;
  constexpr std::uint32_t visible =
      DeviceFieldBit(Ui2DeviceField::MidiDevice) |
      DeviceFieldBit(Ui2DeviceField::Resampler) |
      DeviceFieldBit(Ui2DeviceField::Brightness) |
      DeviceFieldBit(Ui2DeviceField::Theme) |
      DeviceFieldBit(Ui2DeviceField::Font);
  Ui2DeviceController controller(visible, Ui2DeviceField::MidiDevice, 3);

  CHECK(controller.SelectedField() == Ui2DeviceField::MidiDevice);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Resampler);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Brightness);
  CHECK(controller.FirstVisibleOrdinal() == 0U);

  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2DeviceField::Theme);
  CHECK(controller.FirstVisibleOrdinal() == 1U);
  const auto browse = Tap(controller, TrackerAction::Edit);
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
  controller.SetSelector(Ui2DeviceField::Resampler, {2U, 0U, true});

  CHECK(controller.Bottom().kind == Ui2DeviceBottomKind::Selector);
  CHECK(controller.Bottom().count == 4U);
  controller.Handle(TrackerAction::Edit, true);
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  for (std::uint16_t expected = 1; expected <= 3; ++expected) {
    const auto command = Tap(controller, TrackerAction::Right);
    REQUIRE(command.HasValue());
    CHECK(command.type == Ui2DeviceCommandType::SetSelector);
    CHECK(command.field == Ui2DeviceField::MidiDevice);
    CHECK(command.value == expected);
  }
  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
  controller.Handle(TrackerAction::Edit, false);
  CHECK(controller.Selector(Ui2DeviceField::MidiDevice).current == 3U);

  Tap(controller, TrackerAction::Down); // MIDI sync
  Tap(controller, TrackerAction::Down); // Resampler
  CHECK(controller.SelectedField() == Ui2DeviceField::Resampler);
  controller.Handle(TrackerAction::Edit, true);
  const auto wrapped = Tap(controller, TrackerAction::Left);
  REQUIRE(wrapped.HasValue());
  CHECK(wrapped.value == 1U);
  CHECK(controller.Bottom().wrap);
  controller.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 Theme owns NAME actions and every palette row") {
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
    CHECK(Tap(controller, TrackerAction::Edit).type == actions[index]);
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
  CHECK(controller.FirstVisibleOrdinal() == 15U);
  CHECK(controller.Bottom().kind == Ui2ThemeBottomKind::Rgb);
  CHECK(controller.Bottom().selectedIndex == 0U);
  CHECK(controller.Bottom().optionCount == 3U);
  CHECK_FALSE(Tap(controller, TrackerAction::Edit).HasValue());

  Tap(controller, TrackerAction::Left);
  CHECK(controller.ColorComponent() == 2U);
  CHECK(controller.Bottom().selectedIndex == 2U);
  controller.Handle(TrackerAction::Edit, true);
  const auto color = controller.Handle(TrackerAction::Up, true);
  CHECK(color.type == Ui2ThemeCommandType::AdjustColor);
  CHECK(color.color == 19);
  CHECK(color.component == 2U);
  CHECK(color.delta == 10);
  const auto repeated = controller.Handle(TrackerAction::Up, true);
  CHECK(repeated.type == Ui2ThemeCommandType::AdjustColor);
  CHECK(repeated.delta == 10);
  controller.Handle(TrackerAction::Up, false);
  const auto decrement = controller.Handle(TrackerAction::Down, true);
  CHECK(decrement.delta == -10);
  controller.Handle(TrackerAction::Down, false);

  const auto fineDecrement = controller.Handle(TrackerAction::Left, true);
  CHECK(fineDecrement.type == Ui2ThemeCommandType::AdjustColor);
  CHECK(fineDecrement.delta == -1);
  controller.Handle(TrackerAction::Left, false);
  const auto fineIncrement = controller.Handle(TrackerAction::Right, true);
  CHECK(fineIncrement.type == Ui2ThemeCommandType::AdjustColor);
  CHECK(fineIncrement.delta == 1);
  controller.Handle(TrackerAction::Right, false);

  const auto reset = controller.Handle(TrackerAction::Option, true);
  CHECK(reset.type == Ui2ThemeCommandType::ResetColorComponent);
  CHECK(reset.color == 19);
  CHECK(reset.component == 2U);
  controller.Handle(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Edit, false);

  Tap(controller, TrackerAction::Right);
  CHECK(controller.ColorComponent() == 0U);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedColor() == 19);
}

TEST_CASE("UI2 Theme workflow bounds RGB edits for application persistence") {
  using namespace ui2;
  static_assert(Ui2ThemeWorkflow::Colors{}.size() == 20U);

  Ui2ThemeWorkflow::Colors colors{};
  Ui2ThemeWorkflow::Colors defaults{};
  colors[0] = 0x102030U;
  defaults[0] = 0xA0B0C0U;
  Ui2ThemeController controller(0);
  controller.Handle(TrackerAction::Edit, true);
  const Ui2ThemeCommand command = controller.Handle(TrackerAction::Up, true);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Edit, false);

  const Ui2ThemeColorEditResult edit =
      Ui2ThemeWorkflow::Execute(command, colors, defaults);
  REQUIRE(edit.accepted);
  REQUIRE(edit.changed);
  CHECK(edit.color == 0U);
  CHECK(edit.packedColor == 0x1A2030U);
  const std::array<std::uint8_t, 3> expected{0x1AU, 0x20U, 0x30U};
  CHECK(Ui2ThemeWorkflow::Components(edit.packedColor) == expected);

  Ui2ConfigSaveState persistence;
  if (edit.changed)
    persistence.MarkDirty();
  CHECK(persistence.Dirty());

  colors[0] = 0xFF2030U;
  const auto clamped = Ui2ThemeWorkflow::Execute(command, colors, defaults);
  CHECK(clamped.accepted);
  CHECK_FALSE(clamped.changed);
  CHECK(clamped.packedColor == 0xFF2030U);

  const auto greenFloor = Ui2ThemeWorkflow::Execute(
      {.type = Ui2ThemeCommandType::AdjustColor,
       .color = 0,
       .component = 1U,
       .delta = -1000},
      colors, defaults);
  CHECK(greenFloor.accepted);
  CHECK(greenFloor.changed);
  CHECK(greenFloor.packedColor == 0xFF0030U);

  CHECK_FALSE(Ui2ThemeWorkflow::Execute(
                  {.type = Ui2ThemeCommandType::AdjustColor, .color = 20},
                  colors, defaults)
                  .accepted);
  CHECK_FALSE(Ui2ThemeWorkflow::Execute(
                  {.type = Ui2ThemeCommandType::AdjustColor,
                   .color = 0,
                   .component = 3U},
                  colors, defaults)
                  .accepted);

  colors[0] = 0x112233U;
  const auto resetGreen = Ui2ThemeWorkflow::Execute(
      {.type = Ui2ThemeCommandType::ResetColorComponent,
       .color = 0,
       .component = 1U},
      colors, defaults);
  CHECK(resetGreen.accepted);
  CHECK(resetGreen.changed);
  CHECK(resetGreen.packedColor == 0x11B033U);
}

TEST_CASE("UI2 Font keeps text case and exposes BROWSE DEFAULT on the font row") {
  ui2::Ui2FontController controller;
  CHECK(controller.SelectedField() == ui2::Ui2FontField::TextCase);
  CHECK(controller.TextCase() == 1U);
  const auto previous = Tap(controller, TrackerAction::Left);
  CHECK(previous.type == ui2::Ui2FontCommandType::SetTextCase);
  CHECK(previous.value == 0U);
  const auto next = Tap(controller, TrackerAction::Right);
  CHECK(next.type == ui2::Ui2FontCommandType::SetTextCase);
  CHECK(next.value == 1U);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == ui2::Ui2FontField::Font);
  CHECK(controller.SelectedAction() == ui2::Ui2FontAction::Browse);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        ui2::Ui2FontCommandType::BrowseFont);
  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
  CHECK(controller.SelectedAction() == ui2::Ui2FontAction::Default);
  CHECK(Tap(controller, TrackerAction::Edit).type ==
        ui2::Ui2FontCommandType::RestoreDefault);

  controller.SetFeedback(ui2::Ui2FontFeedback::BrowserUnavailable);
  CHECK(controller.Feedback() == ui2::Ui2FontFeedback::BrowserUnavailable);
  Tap(controller, TrackerAction::Up);
  CHECK(controller.SelectedField() == ui2::Ui2FontField::TextCase);
  CHECK(controller.Feedback() == ui2::Ui2FontFeedback::None);
}

TEST_CASE("UI2 Font workflow fails BROWSE closed and restores DEFAULT") {
  using namespace ui2;
  Variable textCase(FourCC::VarUITextCase, 1);
  CHECK(Ui2ExecuteFontCommand(
            {.type = Ui2FontCommandType::SetTextCase, .value = 2U},
            &textCase) == Ui2FontWorkflowResult::TextCaseChanged);
  CHECK(textCase.GetInt() == 2);
  CHECK(Ui2ExecuteFontCommand(
            {.type = Ui2FontCommandType::SetTextCase, .value = 2U},
            &textCase) == Ui2FontWorkflowResult::None);

  CountingFontVariable font;
  font.SetInt(2);
  REQUIRE(font.GetInt() == 2);

  CHECK(Ui2ExecuteFontCommand({.type = Ui2FontCommandType::BrowseFont}, &font) ==
        Ui2FontWorkflowResult::BrowserUnavailable);
  CHECK(font.GetInt() == 2);
  CHECK(font.resetCalls == 0);

  CHECK(Ui2ExecuteFontCommand({.type = Ui2FontCommandType::RestoreDefault},
                              &font) ==
        Ui2FontWorkflowResult::DefaultRestored);
  CHECK(font.GetInt() == 0);
  CHECK(font.resetCalls == 1);

  CHECK(Ui2ExecuteFontCommand({.type = Ui2FontCommandType::RestoreDefault},
                              &font) == Ui2FontWorkflowResult::None);
  CHECK(font.GetInt() == 0);
  CHECK(font.resetCalls == 1);

  Variable unrelated(FourCC::VarUITextCase, 2);
  CHECK(Ui2ExecuteFontCommand({.type = Ui2FontCommandType::RestoreDefault},
                              &unrelated) ==
        Ui2FontWorkflowResult::ConfigUnavailable);
  CHECK(unrelated.GetInt() == 2);
  CHECK(Ui2ExecuteFontCommand({.type = Ui2FontCommandType::RestoreDefault},
                              nullptr) ==
        Ui2FontWorkflowResult::ConfigUnavailable);
}

TEST_CASE("UI2 Rename owns its bounded draft and full-page navigation") {
  using namespace ui2;
  Ui2RenameController controller;
  controller.Begin("ONECYCAC", 16U);
  CHECK(controller.Active());
  CHECK(controller.Snapshot().focus == UiDialogFocus::Input);

  Tap(controller, TrackerAction::Edit);
  CHECK(controller.Snapshot().focus == UiDialogFocus::Keyboard);
  Tap(controller, TrackerAction::Edit);
  CHECK(std::string_view(controller.Value()) == "ONECYCAC1");
  Tap(controller, TrackerAction::Option);
  CHECK(std::string_view(controller.Value()) == "ONECYCAC");

  for (int row = 0; row < 5; ++row)
    Tap(controller, TrackerAction::Down);
  CHECK(controller.Snapshot().focus == UiDialogFocus::Actions);
  CHECK(Tap(controller, TrackerAction::Edit) == Ui2RenameCommand::Save);
  CHECK_FALSE(controller.Active());
}

TEST_CASE("UI2 Rename waits for the opening EDIT release") {
  using namespace ui2;
  Ui2RenameController controller;

  SUBCASE("held UP cannot jump directly to actions") {
    controller.Begin("ONECYCAC", 16U, nullptr, TrackerAction::Edit);
    CHECK(controller.Snapshot().focus == UiDialogFocus::Input);
    CHECK(controller.Handle(TrackerAction::Up, true) ==
          Ui2RenameCommand::None);
    CHECK(controller.Handle(TrackerAction::Up, false) ==
          Ui2RenameCommand::None);
    CHECK(controller.Snapshot().focus == UiDialogFocus::Input);
    CHECK(controller.Handle(TrackerAction::Edit, false) ==
          Ui2RenameCommand::None);
    Tap(controller, TrackerAction::Up);
    CHECK(controller.Snapshot().focus == UiDialogFocus::Actions);
  }

  SUBCASE("held DOWN cannot jump directly to the keyboard") {
    controller.Begin("ONECYCAC", 16U, nullptr, TrackerAction::Edit);
    CHECK(controller.Handle(TrackerAction::Down, true) ==
          Ui2RenameCommand::None);
    CHECK(controller.Handle(TrackerAction::Down, false) ==
          Ui2RenameCommand::None);
    CHECK(controller.Handle(TrackerAction::Edit, true) ==
          Ui2RenameCommand::None);
    CHECK(controller.Snapshot().focus == UiDialogFocus::Input);
    CHECK(controller.Handle(TrackerAction::Edit, false) ==
          Ui2RenameCommand::None);
    Tap(controller, TrackerAction::Down);
    CHECK(controller.Snapshot().focus == UiDialogFocus::Keyboard);
  }
}

TEST_CASE("UI2 Rename disables save for visually empty names") {
  using namespace ui2;
  Ui2RenameController controller;
  controller.Begin("", 16U);
  CHECK_FALSE(controller.Snapshot().saveEnabled);

  // Input -> actions. SAVE is skipped while the draft is empty.
  Tap(controller, TrackerAction::Up);
  CHECK(controller.Snapshot().selectedAction == 1U);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Snapshot().selectedAction == 0U);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK(Tap(controller, TrackerAction::Edit) == Ui2RenameCommand::Randomize);

  controller.Begin(" ", 16U);
  CHECK_FALSE(controller.Snapshot().saveEnabled);
  Tap(controller, TrackerAction::Up);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Snapshot().selectedAction == 0U);
  Tap(controller, TrackerAction::Right);
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK(controller.Active());
}

TEST_CASE("UI2 Mixer selects nine strips and edits volume with Enter") {
  using namespace ui2;
  Ui2MixerController controller;
  for (std::uint8_t channel = 1U; channel < Ui2MixerController::ChannelCount;
       ++channel) {
    const auto command = Tap(controller, TrackerAction::Right);
    CHECK(command.type == Ui2MixerCommandType::SelectChannel);
    CHECK(command.channel == channel);
  }
  CHECK(controller.SelectedChannel() == 8U);
  controller.Handle(TrackerAction::Edit, true);
  const auto adjust = Tap(controller, TrackerAction::Up);
  CHECK(adjust.type == Ui2MixerCommandType::AdjustVolume);
  CHECK(adjust.channel == 8U);
  CHECK(adjust.delta == 1);
  controller.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 Mixer plain and Shift PLAY share global song transport") {
  using namespace ui2;
  Ui2MixerController controller;
  controller.Synchronize(4U);

  const auto plain = Tap(controller, TrackerAction::Play);
  REQUIRE(plain.type != Ui2MixerCommandType::None);
  CHECK(plain.type == Ui2MixerCommandType::StartPlayback);
  CHECK(plain.channel == 4U);
  CHECK(controller.SelectedChannel() == 4U);

  controller.Handle(TrackerAction::Shift, true);
  const auto shifted = Tap(controller, TrackerAction::Play);
  REQUIRE(shifted.type != Ui2MixerCommandType::None);
  CHECK(shifted.type == Ui2MixerCommandType::StartPlayback);
  CHECK(shifted.channel == 4U);
  CHECK(controller.SelectedChannel() == 4U);
  controller.Handle(TrackerAction::Shift, false);

  controller.Handle(TrackerAction::Option, true);
  const auto solo = Tap(controller, TrackerAction::Play);
  CHECK(solo.type == Ui2MixerCommandType::ToggleSolo);
  CHECK(solo.channel == 4U);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 global song transport toggles at the current Song cursor") {
  using namespace ui2;
  FakeSongTransport transport;

  Ui2ToggleSongTransportAtCursor(transport, FakePlayMode::Song, 5, 8U);
  CHECK(transport.buttonCalls == 1);
  CHECK(transport.startCalls == 1);
  CHECK(transport.stopCalls == 0);
  CHECK(transport.running);
  CHECK(transport.lastOrigin == FakePlayMode::Song);
  CHECK(transport.lastFrom == 5U);
  CHECK_FALSE(transport.lastStartFromPrevious);
  CHECK(transport.lastChainPosition == 5U);

  Ui2ToggleSongTransportAtCursor(transport, FakePlayMode::Song, 2, 8U);
  CHECK(transport.buttonCalls == 2);
  CHECK(transport.startCalls == 1);
  CHECK(transport.stopCalls == 1);
  CHECK_FALSE(transport.running);
  CHECK(transport.lastOrigin == FakePlayMode::Song);
  CHECK(transport.lastFrom == 2U);
  CHECK_FALSE(transport.lastStartFromPrevious);
  CHECK(transport.lastChainPosition == 2U);

  CHECK(transport.muteMask == 0xA5U);
  CHECK(transport.soloMask == 0x24U);
  CHECK(transport.vuGeneration == 73U);
}

TEST_CASE("UI2 Mixer synchronizes the shared track and never aliases Master mute") {
  using namespace ui2;
  Ui2MixerController controller;
  controller.Synchronize(5U);
  CHECK(controller.SelectedChannel() == 5U);

  controller.Handle(TrackerAction::Option, true);
  const auto solo = Tap(controller, TrackerAction::Play);
  CHECK(solo.type == Ui2MixerCommandType::ToggleSolo);
  CHECK(solo.channel == 5U);
  controller.Handle(TrackerAction::Option, false);

  controller.Synchronize(8U);
  controller.Handle(TrackerAction::Option, true);
  CHECK(Tap(controller, TrackerAction::Play).type ==
        Ui2MixerCommandType::None);
  controller.Handle(TrackerAction::Shift, true);
  const auto clearAll = Tap(controller, TrackerAction::Play);
  CHECK(clearAll.type == Ui2MixerCommandType::UnmuteAll);
  CHECK(clearAll.channel == 8U);
  controller.Handle(TrackerAction::Shift, false);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Record edits persisted fields without a legacy FieldView") {
  using namespace ui2;
  Ui2RecordController controller(-12, 12, -6, 6);
  controller.SetAvailable(true);
  controller.Synchronize(1U, 0, 0);
  const auto source = Tap(controller, TrackerAction::Right);
  CHECK(source.type == Ui2RecordCommandType::SetSource);
  CHECK(source.value == 2);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2RecordField::LineGain);
  const auto fine = Tap(controller, TrackerAction::Right);
  CHECK(fine.type == Ui2RecordCommandType::SetLineGain);
  CHECK(fine.value == 1);
  controller.Handle(TrackerAction::Edit, true);
  const auto coarse = Tap(controller, TrackerAction::Up);
  CHECK(coarse.type == Ui2RecordCommandType::SetLineGain);
  CHECK(coarse.value == 3);
  controller.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 Record gain ranges clamp synchronization and stop at bounds") {
  using namespace ui2;
  Ui2RecordController controller(-4, 4, -2, 2);
  controller.SetAvailable(true);
  controller.Synchronize(1U, 99, -99);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2RecordField::LineGain);
  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
  REQUIRE(Tap(controller, TrackerAction::Left).value == 3);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2RecordField::MicGain);
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  REQUIRE(Tap(controller, TrackerAction::Right).value == -1);
}

TEST_CASE("UI2 Record zero-range adapters cannot emit gain writes") {
  using namespace ui2;
  Ui2RecordController controller;
  controller.SetAvailable(true);
  controller.Synchronize(1U, 99, -99);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2RecordField::LineGain);
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
  controller.Handle(TrackerAction::Edit, true);
  CHECK_FALSE(Tap(controller, TrackerAction::Up).HasValue());
  controller.Handle(TrackerAction::Edit, false);

  Tap(controller, TrackerAction::Down);
  CHECK(controller.SelectedField() == Ui2RecordField::MicGain);
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
}

TEST_CASE("UI2 Record unavailable capability is read-only") {
  using namespace ui2;
  Ui2RecordController controller(-12, 12, -6, 6);
  controller.Synchronize(1U, 0, 0);
  CHECK_FALSE(controller.Available());

  CHECK_FALSE(Tap(controller, TrackerAction::Right).HasValue());
  CHECK_FALSE(Tap(controller, TrackerAction::Down).HasValue());
  CHECK_FALSE(Tap(controller, TrackerAction::Play).HasValue());
  controller.Handle(TrackerAction::Edit, true);
  CHECK_FALSE(Tap(controller, TrackerAction::Up).HasValue());
  controller.Handle(TrackerAction::Edit, false);
  CHECK(controller.SelectedField() == Ui2RecordField::Source);
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
    CHECK(Tap(controller, TrackerAction::Edit).type == actions[index]);
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

TEST_CASE("UI2 Instrument modal handoff releases its triggering arrow") {
  using namespace ui2;
  Ui2InstrumentController controller;
  Tap(controller, TrackerAction::Down);
  controller.SetTypeSelector({5U, 1U, true});
  const auto first = controller.Handle(TrackerAction::Right, true);
  REQUIRE(first.type == Ui2InstrumentCommandType::SetType);

  controller.ReleaseHeldInput();
  const auto afterModal = controller.Handle(TrackerAction::Right, true);
  CHECK(afterModal.type == Ui2InstrumentCommandType::SetType);
  CHECK(afterModal.value == 3);
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
  controller.Handle(TrackerAction::Option, true);
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
  CHECK(number.value == MAX_INSTRUMENT_COUNT - 1U);
  CHECK(controller.Number() == MAX_INSTRUMENT_COUNT - 1U);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Option, false);
  CHECK_FALSE(controller.NumberFocus());
}

TEST_CASE("UI2 Instrument traverses and synchronizes every model slot") {
  using namespace ui2;
  CHECK(Ui2InstrumentController::DefaultInstrumentCount ==
        MAX_INSTRUMENT_COUNT);

  if constexpr (MAX_INSTRUMENT_COUNT > 0x27) {
    Ui2InstrumentController adjacent(0x26U);
    adjacent.Handle(TrackerAction::Option, true);
    const auto next = Tap(adjacent, TrackerAction::Down);
    REQUIRE(next.HasValue());
    CHECK(next.type == Ui2InstrumentCommandType::SelectNumber);
    CHECK(next.value == 0x27);
    CHECK(adjacent.Number() == 0x27U);
    adjacent.Handle(TrackerAction::Option, false);
  }

  Ui2InstrumentController high;
  high.Synchronize(MAX_INSTRUMENT_COUNT - 1U, 0U, {5U, 0U, true}, 0U, 0U);
  CHECK(high.Number() == MAX_INSTRUMENT_COUNT - 1U);
  if constexpr (MAX_INSTRUMENT_COUNT == 0x40)
    CHECK(high.Number() == 0x3FU);
  high.Handle(TrackerAction::Option, true);
  const auto wrapped = Tap(high, TrackerAction::Down);
  REQUIRE(wrapped.HasValue());
  CHECK(wrapped.type == Ui2InstrumentCommandType::SelectNumber);
  CHECK(wrapped.value == 0x00);
  CHECK(high.Number() == 0x00U);
  high.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Instrument Enter emits typed field edits and one commit") {
  using namespace ui2;
  Ui2InstrumentController controller(
      0, 0, 8, 0, {Ui2InstrumentCursorKind::Field, 3});
  const auto activate = controller.Handle(TrackerAction::Edit, true);
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
  const auto commit = controller.Handle(TrackerAction::Edit, false);
  CHECK(commit.type == Ui2InstrumentCommandType::CommitValueEdits);
  CHECK_FALSE(controller.Handle(TrackerAction::Edit, false).HasValue());
}

TEST_CASE("UI2 Instrument Enter moves a bounded component cursor") {
  using namespace ui2;
  Ui2InstrumentController controller(
      0, 0, 3, 6, {Ui2InstrumentCursorKind::Operator1, 2},
      {5U, 4U, true});
  controller.ConfigureValueSubfields(Ui2InstrumentSubfieldMode::HexDigit, 4U);
  CHECK(controller.Subfield() == 3U);
  controller.Handle(TrackerAction::Edit, true);
  CHECK(controller.EnterSubfieldFocus());

  CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
  CHECK(controller.Subfield() == 2U);
  controller.Handle(TrackerAction::Left, false);
  const auto adjust = controller.Handle(TrackerAction::Up, true);
  REQUIRE(adjust.HasValue());
  CHECK(adjust.type == Ui2InstrumentCommandType::AdjustField);
  CHECK(adjust.subfieldMode == Ui2InstrumentSubfieldMode::HexDigit);
  CHECK(adjust.subfield == 2U);
  controller.Handle(TrackerAction::Up, false);
  CHECK(controller.Handle(TrackerAction::Edit, false).type ==
        Ui2InstrumentCommandType::CommitValueEdits);
}

TEST_CASE("UI2 Groove owns sixteen wrapping rows outside selection mode") {
  using namespace ui2;
  Ui2GrooveController controller(0, 0);
  CHECK_FALSE(controller.BottomVisible());
  Tap(controller, TrackerAction::Up);
  CHECK(controller.Row() == 15U);
  Tap(controller, TrackerAction::Down);
  CHECK(controller.Row() == 0U);

  CHECK(Tap(controller, TrackerAction::Edit).type ==
        Ui2GrooveCommandType::InitializeStep);
  controller.Handle(TrackerAction::Edit, true);
  const auto adjust = controller.Handle(TrackerAction::Up, true);
  REQUIRE(adjust.HasValue());
  CHECK(adjust.type == Ui2GrooveCommandType::AdjustStep);
  CHECK(adjust.value == 1);
  CHECK(adjust.synchronized);
  controller.Handle(TrackerAction::Up, false);
  controller.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 Groove selection follows the grid clipboard lifecycle") {
  using namespace ui2;
  Ui2GrooveController controller(0, 3);

  controller.Handle(TrackerAction::Shift, true);
  CHECK_FALSE(Tap(controller, TrackerAction::Option).HasValue());
  controller.Handle(TrackerAction::Shift, false);
  REQUIRE(controller.Selection().active);
  CHECK(controller.Selection().SingleCell());
  CHECK(controller.Selection().Top() == 3U);
  CHECK(controller.BottomVisible());

  CHECK_FALSE(Tap(controller, TrackerAction::Down).HasValue());
  CHECK(controller.Row() == 4U);
  CHECK(controller.Selection().Top() == 3U);
  CHECK(controller.Selection().Bottom() == 4U);

  const Ui2GrooveCommand copy = Tap(controller, TrackerAction::Option);
  REQUIRE(copy.type == Ui2GrooveCommandType::CopySelection);
  CHECK(copy.selection.active);
  CHECK(copy.selection.Top() == 3U);
  CHECK(copy.selection.Bottom() == 4U);
  CHECK_FALSE(controller.Selection().active);
  CHECK_FALSE(controller.BottomVisible());

  controller.Handle(TrackerAction::Shift, true);
  const Ui2GrooveCommand paste = Tap(controller, TrackerAction::Edit);
  controller.Handle(TrackerAction::Shift, false);
  REQUIRE(paste.type == Ui2GrooveCommandType::PasteSelection);
  CHECK(paste.row == 4U);
}

TEST_CASE("UI2 Groove selection cuts interpolates and stays in fixed bounds") {
  using namespace ui2;
  Ui2GrooveController controller(0, 0);

  controller.Handle(TrackerAction::Shift, true);
  Tap(controller, TrackerAction::Option);
  controller.Handle(TrackerAction::Shift, false);
  REQUIRE(controller.Selection().active);
  CHECK_FALSE(Tap(controller, TrackerAction::Up).HasValue());
  CHECK(controller.Row() == 0U);
  CHECK(controller.Selection().Top() == 0U);
  CHECK_FALSE(Tap(controller, TrackerAction::Down).HasValue());
  CHECK_FALSE(Tap(controller, TrackerAction::Down).HasValue());
  CHECK(controller.Selection().Bottom() == 2U);

  controller.Handle(TrackerAction::Shift, true);
  const Ui2GrooveCommand interpolate = Tap(controller, TrackerAction::Edit);
  controller.Handle(TrackerAction::Shift, false);
  REQUIRE(interpolate.type == Ui2GrooveCommandType::InterpolateSelection);
  CHECK(interpolate.selection.Top() == 0U);
  CHECK(interpolate.selection.Bottom() == 2U);
  CHECK(controller.Selection().active);

  controller.Handle(TrackerAction::Edit, true);
  const Ui2GrooveCommand cut = controller.Handle(TrackerAction::Option, true);
  REQUIRE(cut.type == Ui2GrooveCommandType::CutSelection);
  CHECK(cut.selection.active);
  CHECK_FALSE(controller.Selection().active);
  controller.Handle(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Edit, false);
}

TEST_CASE("UI2 Groove Edit clears cells and wraps the groove number") {
  using namespace ui2;
  Ui2GrooveController controller(0, 4);
  controller.Handle(TrackerAction::Edit, true);
  const auto clear = controller.Handle(TrackerAction::Option, true);
  REQUIRE(clear.HasValue());
  CHECK(clear.type == Ui2GrooveCommandType::ClearStep);
  CHECK(clear.row == 4U);
  controller.Handle(TrackerAction::Option, false);
  controller.Handle(TrackerAction::Edit, false);

  controller.Handle(TrackerAction::Option, true);
  const auto number = controller.Handle(TrackerAction::Left, true);
  REQUIRE(number.HasValue());
  CHECK(number.type == Ui2GrooveCommandType::SelectNumber);
  CHECK(number.value == 31);
  CHECK(controller.Number() == 31U);
  controller.Handle(TrackerAction::Left, false);
  controller.Handle(TrackerAction::Option, false);
}

TEST_CASE("UI2 Groove accepts both local and global playback chords") {
  using namespace ui2;
  struct PlaybackCase {
    bool shift;
    bool songTransport;
  };
  constexpr PlaybackCase cases[] = {{false, false}, {true, true}};

  for (const PlaybackCase &playbackCase : cases) {
    CAPTURE(playbackCase.shift);
    Ui2GrooveController controller;
    if (playbackCase.shift)
      controller.Handle(TrackerAction::Shift, true);
    const Ui2GrooveCommand command = Tap(controller, TrackerAction::Play);
    CHECK(command.type == Ui2GrooveCommandType::StartPlayback);
    CHECK(command.songTransport == playbackCase.songTransport);
  }
}

TEST_CASE("UI2 Groove accepts M8 solo and clear-all playback chords") {
  using namespace ui2;
  struct PerformanceCase {
    bool shift;
    Ui2GrooveCommandType type;
  };
  constexpr PerformanceCase cases[] = {
      {false, Ui2GrooveCommandType::ToggleSolo},
      {true, Ui2GrooveCommandType::UnmuteAll},
  };

  for (const PerformanceCase &performanceCase : cases) {
    CAPTURE(performanceCase.shift);
    Ui2GrooveController controller;
    if (performanceCase.shift)
      controller.Handle(TrackerAction::Shift, true);
    controller.Handle(TrackerAction::Option, true);
    const Ui2GrooveCommand command = Tap(controller, TrackerAction::Play);
    CHECK(command.type == performanceCase.type);
    CHECK_FALSE(command.songTransport);
  }
}

TEST_CASE("UI2 Groove step policy initializes only empty cells and clamps") {
  using namespace ui2;
  CHECK(Ui2GrooveStepPolicy::Initialize(0xFFU) == 6U);
  CHECK(Ui2GrooveStepPolicy::Initialize(9U) == 9U);
  CHECK(Ui2GrooveStepPolicy::Adjust(0xFFU, -1) == 1U);
  CHECK(Ui2GrooveStepPolicy::Adjust(1U, -1) == 1U);
  CHECK(Ui2GrooveStepPolicy::Adjust(15U, 1) == 15U);
  CHECK(Ui2GrooveStepPolicy::Adjust(7U, 1) == 8U);
}

TEST_CASE("UI2 Groove workflow reports only effective mutations") {
  using namespace ui2;
  std::uint8_t steps[Ui2GrooveController::RowCount]{};
  steps[3] = Ui2GrooveStepPolicy::Empty;

  auto result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::InitializeStep, .row = 3}, steps);
  CHECK(result.projectMutated);
  CHECK(steps[3] == Ui2GrooveStepPolicy::Initial);

  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::InitializeStep, .row = 3}, steps);
  CHECK_FALSE(result.projectMutated);

  steps[4] = 15U;
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::AdjustStep, .value = 1, .row = 4},
      steps);
  CHECK_FALSE(result.projectMutated);

  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::SelectNumber}, steps);
  CHECK(result.selectNumber);
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::StartPlayback}, steps);
  CHECK(result.dispatchPerformance);
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::ToggleSolo}, steps);
  CHECK(result.dispatchPerformance);
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::UnmuteAll}, steps);
  CHECK(result.dispatchPerformance);
}

TEST_CASE("UI2 Groove workflow copies cuts and clips selection pastes") {
  using namespace ui2;
  std::uint8_t steps[Ui2GrooveController::RowCount]{};
  for (std::uint8_t row = 0U; row < Ui2GrooveController::RowCount; ++row)
    steps[row] = static_cast<std::uint8_t>(row + 1U);
  Ui2GrooveClipboard clipboard;
  Ui2GridSelectionState selection;
  selection.Begin(0U, 2U);
  selection.Follow(0U, 4U);

  auto result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::CopySelection,
       .selection = selection},
      steps, clipboard);
  CHECK_FALSE(result.projectMutated);
  REQUIRE(clipboard.count == 3U);
  CHECK(clipboard.steps[0] == 3U);
  CHECK(clipboard.steps[1] == 4U);
  CHECK(clipboard.steps[2] == 5U);

  steps[15] = Ui2GrooveStepPolicy::Empty;
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::PasteSelection, .row = 15U}, steps,
      clipboard);
  CHECK(result.projectMutated);
  CHECK(steps[15] == 3U);

  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::CutSelection,
       .selection = selection},
      steps, clipboard);
  CHECK(result.projectMutated);
  CHECK(steps[2] == Ui2GrooveStepPolicy::Empty);
  CHECK(steps[3] == Ui2GrooveStepPolicy::Empty);
  CHECK(steps[4] == Ui2GrooveStepPolicy::Empty);
  REQUIRE(clipboard.count == 3U);
  CHECK(clipboard.steps[0] == 3U);
  CHECK(clipboard.steps[1] == 4U);
  CHECK(clipboard.steps[2] == 5U);
}

TEST_CASE("UI2 Groove interpolation is linear and rejects empty endpoints") {
  using namespace ui2;
  std::uint8_t steps[Ui2GrooveController::RowCount]{};
  steps[2] = 3U;
  steps[3] = Ui2GrooveStepPolicy::Empty;
  steps[4] = Ui2GrooveStepPolicy::Empty;
  steps[5] = Ui2GrooveStepPolicy::Empty;
  steps[6] = 11U;
  Ui2GridSelectionState selection;
  selection.Begin(0U, 2U);
  selection.Follow(0U, 6U);
  Ui2GrooveClipboard clipboard;

  auto result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::InterpolateSelection,
       .selection = selection},
      steps, clipboard);
  CHECK(result.projectMutated);
  CHECK(steps[2] == 3U);
  CHECK(steps[3] == 5U);
  CHECK(steps[4] == 7U);
  CHECK(steps[5] == 9U);
  CHECK(steps[6] == 11U);

  steps[6] = Ui2GrooveStepPolicy::Empty;
  const auto before = std::array<std::uint8_t, 5U>{
      steps[2], steps[3], steps[4], steps[5], steps[6]};
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::InterpolateSelection,
       .selection = selection},
      steps, clipboard);
  CHECK_FALSE(result.projectMutated);
  CHECK(std::equal(before.begin(), before.end(), steps + 2U));
}

TEST_CASE("UI2 Groove coarse edits preserve each adjacent pair's tick sum") {
  using namespace ui2;
  std::uint8_t steps[Ui2GrooveController::RowCount]{};
  steps[0] = 6U;
  steps[1] = 6U;

  auto result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::AdjustStep,
       .value = 1,
       .row = 0,
       .synchronized = true},
      steps);
  CHECK(result.projectMutated);
  CHECK(steps[0] == 7U);
  CHECK(steps[1] == 5U);

  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::AdjustStep,
       .value = 1,
       .row = 1,
       .synchronized = true},
      steps);
  CHECK(result.projectMutated);
  CHECK(steps[0] == 6U);
  CHECK(steps[1] == 6U);

  steps[0] = 15U;
  steps[1] = 1U;
  result = Ui2GrooveWorkflow::Execute(
      {.type = Ui2GrooveCommandType::AdjustStep,
       .value = 1,
       .row = 0,
       .synchronized = true},
      steps);
  CHECK_FALSE(result.projectMutated);
  CHECK(steps[0] == 15U);
  CHECK(steps[1] == 1U);
}

TEST_CASE("UI2 settings controllers keep fixed-capacity trivial state") {
  using namespace ui2;
  CHECK(std::is_trivially_copyable_v<Ui2DeviceController>);
  CHECK(std::is_trivially_copyable_v<Ui2ThemeController>);
  CHECK(std::is_trivially_copyable_v<Ui2FontController>);
  CHECK(std::is_trivially_copyable_v<Ui2InstrumentController>);
  CHECK(std::is_trivially_copyable_v<Ui2GrooveController>);
  CHECK(std::is_trivially_copyable_v<Ui2RecordController>);
  CHECK(std::is_trivially_copyable_v<Ui2SettingsBrowserController>);
  CHECK(sizeof(Ui2DeviceController) <= 80U);
  CHECK(sizeof(Ui2ThemeController) <= 16U);
  CHECK(sizeof(Ui2FontController) <= 8U);
  CHECK(sizeof(Ui2InstrumentController) <= 40U);
  CHECK(sizeof(Ui2GrooveController) <= 16U);
  CHECK(sizeof(Ui2RecordController) <= 16U);
  CHECK(sizeof(Ui2SettingsBrowserController) <= 1'100U);
}
