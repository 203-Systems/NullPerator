#include "Application/Instruments/ChiptuneInstrument.h"
#include "Application/Instruments/DrumInstrument.h"
#include "Application/Instruments/InstrumentBankRestorePolicy.h"
#include "Application/Instruments/StackInstrument.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Application/UI2/Ui2NotePresentation.h"
#include "doctest/doctest.h"
#include <algorithm>
#include <array>

TEST_CASE_TEMPLATE(
    "Coping instruments isolate voices and preserve block continuity", Synth,
    DrumInstrument, StackInstrument, ChiptuneInstrument) {
  Synth whole, split;
  std::array<fixed, 2048> a{}, b{};
  CHECK_FALSE(whole.Render(0, a.data(), 1024, false));
  CHECK_FALSE(whole.Start(-1, 60));
  CHECK_FALSE(whole.Start(SONG_CHANNEL_COUNT, 60));
  CHECK_FALSE(whole.Start(0, 255));
  REQUIRE(whole.Start(0, 60));
  REQUIRE(split.Start(0, 60));
  REQUIRE(whole.Start(1, 64));
  whole.Stop(1);
  REQUIRE(whole.Render(0, a.data(), 1024, false));
  for (int i = 0; i < 16; ++i)
    REQUIRE(split.Render(0, b.data() + i * 128, 64, false));
  CHECK(a == b);
  CHECK(std::any_of(a.begin(), a.end(), [](fixed x) { return x != 0; }));
  whole.ProcessCommand(0, FourCC::InstrumentCommandKill, 0);
  CHECK_FALSE(whole.Render(0, a.data(), 1024, false));
  CHECK(split.Render(0, b.data(), 1024, false));
  split.OnStart();
  CHECK_FALSE(split.Render(0, b.data(), 1024, false));
}

TEST_CASE("Stack GateOff releases the voice") {
  StackInstrument synth;
  REQUIRE(synth.Start(0, 60));
  std::array<fixed, 512> buffer{};
  REQUIRE(synth.Render(0, buffer.data(), 256, false));
  synth.ProcessCommand(0, FourCC::InstrumentCommandGateOff, 0);
  for (int i = 0; i < 200; ++i)
    synth.Render(0, buffer.data(), 256, false);
  CHECK_FALSE(synth.Render(0, buffer.data(), 256, false));
}

TEST_CASE("Drum and Stack have independent fixed restore capacities") {
  InstrumentBankRestorePolicy policy;
  for (int i = 0; i < MAX_DRUMINSTRUMENT_COUNT; ++i)
    CHECK(policy.Reserve(i, IT_DRUM));
  CHECK_FALSE(policy.Reserve(MAX_DRUMINSTRUMENT_COUNT, IT_DRUM));
  for (int i = 0; i < MAX_STACKINSTRUMENT_COUNT; ++i)
    CHECK(policy.Reserve(MAX_DRUMINSTRUMENT_COUNT + i, IT_STACK));
  CHECK_FALSE(policy.Reserve(MAX_DRUMINSTRUMENT_COUNT + MAX_STACKINSTRUMENT_COUNT, IT_STACK));
}

TEST_CASE_TEMPLATE(
    "Coping UI descriptors bind every real parameter exactly once", Synth,
    DrumInstrument, StackInstrument, ChiptuneInstrument) {
  Synth synth;
  const auto type = synth.GetType();
  REQUIRE(ui2::Ui2InstrumentFieldCount(type) == synth.Variables()->size());
  for (std::uint8_t i = 0; i < ui2::Ui2InstrumentFieldCount(type); ++i) {
    const auto descriptor = ui2::Ui2InstrumentFieldParameter(type, i);
    REQUIRE(descriptor.Valid());
    auto *value = synth.FindVariable(descriptor.primary);
    REQUIRE(value != nullptr);
    CHECK(value->GetInt() >= (descriptor.offValue ? VAR_OFF : descriptor.minimum));
    CHECK(value->GetInt() <= descriptor.maximum);
    for (std::uint8_t j = 0; j < i; ++j)
      CHECK(descriptor.primary != ui2::Ui2InstrumentFieldParameter(type, j).primary);
  }
}

TEST_CASE("Coping UI edits drum nibbles and wraps all Stack waves") {
  using namespace ui2;
  const auto drum = Ui2InstrumentFieldParameter(IT_DRUM, 0);
  const auto spec = Ui2InstrumentSubfields(drum);
  REQUIRE(spec.count == 4);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            drum, 0x4562, spec.mode, 2, Ui2InstrumentValueDirection::Right) ==
        0x4572);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(drum, 0x4562, spec.mode, 2,
                                             Ui2InstrumentValueDirection::Up) ==
        0x45F2);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            drum, 0x4562, spec.mode, 2, Ui2InstrumentValueDirection::Down) ==
        0x4502);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            drum, 0x4567, spec.mode, 3, Ui2InstrumentValueDirection::Right) ==
        0x4560);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            drum, 0x4560, spec.mode, 3, Ui2InstrumentValueDirection::Left) ==
        0x4567);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(drum, 0x456F, spec.mode, 3,
                                             Ui2InstrumentValueDirection::Up) ==
        0x4560);
  const auto wave = Ui2InstrumentFieldParameter(IT_STACK, 0);
  CHECK(Ui2AdjustInstrumentParameter(wave, 6, Ui2InstrumentValueDirection::Right) == 0);
  CHECK(Ui2AdjustInstrumentParameter(wave, 0, Ui2InstrumentValueDirection::Left) == 6);
  const auto transpose = Ui2InstrumentFieldParameter(IT_STACK, 3);
  CHECK(Ui2AdjustInstrumentParameter(transpose, 0, Ui2InstrumentValueDirection::Up) == 12);
  CHECK(Ui2AdjustInstrumentParameter(transpose, 24, Ui2InstrumentValueDirection::Up) == 24);
}

TEST_CASE("Drum note presentation and editing keep the legacy stored octave") {
  DrumInstrument drum;
  std::array<char, 5> text{};
  for (unsigned char note = 0; note <= HIGHEST_NOTE; ++note) {
    ui2::FormatUiNote(note, text, &drum);
    const unsigned slot = note % 12 + 1;
    CHECK(text[0] == 'D');
    CHECK(text[1] == '0' + slot / 10);
    CHECK(text[2] == '0' + slot % 10);
    unsigned char edited = NO_NOTE;
    REQUIRE(drum.EditNote(note, 1, false, edited));
    CHECK(edited / 12 == note / 12);
    CHECK(edited % 12 == (note % 12 + 1) % 12);
    REQUIRE(drum.EditNote(note, -1, true, edited));
    CHECK(edited % 12 == (note % 12 + 24 - 10) % 12);
  }
  ui2::FormatUiNote(NO_NOTE, text, &drum);
  CHECK(std::string_view(text.data()) == "---");
  ui2::FormatUiNote(NOTE_OFF, text, &drum);
  CHECK(std::string_view(text.data()) == "OFF");
}

TEST_CASE(
    "Chiptune reserves two slots without affecting existing type capacities") {
  InstrumentBankRestorePolicy policy;
  CHECK(policy.Reserve(0, IT_CHIPTUNE));
  CHECK(policy.Reserve(1, IT_CHIPTUNE));
  CHECK_FALSE(policy.Reserve(2, IT_CHIPTUNE));
  CHECK(policy.Reserve(2, IT_STACK));
  CHECK(policy.Reserve(3, IT_DRUM));
}

TEST_CASE_TEMPLATE("SIP changes the playing voice and resets on a fresh note",
                   Synth, StackInstrument, ChiptuneInstrument) {
  Synth synth, reference;
  std::array<fixed, 2048> changed{}, expected{};
  REQUIRE(synth.Start(0, 60));
  synth.ProcessCommand(0, FourCC::InstrumentCommandSetInstrumentParameter,
                       0x010C);
  REQUIRE(synth.Render(0, changed.data(), 1024, false));
  REQUIRE(reference.Start(0, 60));
  REQUIRE(reference.Render(0, expected.data(), 1024, false));
  CHECK(changed != expected);
  REQUIRE(synth.Start(0, 60));
  REQUIRE(synth.Render(0, changed.data(), 1024, false));
  CHECK(changed == expected);
  synth.Stop(0);
  synth.ProcessCommand(0, FourCC::InstrumentCommandSetInstrumentParameter,
                       0x0003);
  CHECK_FALSE(synth.Render(0, changed.data(), 1024, false));
}

#include "Application/Instruments/SampleRenderingParams.h"
TEST_CASE("Sample VIB composes with pitch and ignores tempo tick frequency") {
  Vibrato first, second;
  first.SetData(64, 255);
  second.SetData(64, 255);
  first.Enable();
  second.Enable();
  bool changes = false;
  for (int i = 0; i < 200; ++i) {
    first.Trigger(false);
    second.Trigger(false);
    for (int extra = 0; extra < i % 7; ++extra)
      second.Trigger(true);
    RUParams a{}, b{};
    a.speedOffset_ = b.speedOffset_ = FP_ONE * 2;
    first.UpdateSRP(a);
    second.UpdateSRP(b);
    CHECK(a.speedOffset_ == b.speedOffset_);
    CHECK(a.speedOffset_ > FP_ONE);
    changes |= a.speedOffset_ != FP_ONE * 2;
  }
  CHECK(changes);
  first.SetData(0, 0);
  RUParams a{};
  a.speedOffset_ = FP_ONE;
  first.Trigger(false);
  first.UpdateSRP(a);
  CHECK(a.speedOffset_ == FP_ONE);
  first.SetData(255, 255);
  first.Disable();
  first.Trigger(false);
  first.UpdateSRP(a);
  CHECK(a.speedOffset_ == FP_ONE);
  renderParams params{};
  for (auto *updater : std::array<I_SRPUpdater *, 9>{
           &params.volumeRamp_, &params.panner_, &params.cutRamp_,
           &params.resRamp_, &params.speedRamp_, &params.legato_, &params.pfin_,
           &params.arp_, &params.vibrato_})
    params.activeUpdaters_.push_back(updater);
  CHECK(params.activeUpdaters_.size() == 9);
}

TEST_CASE_TEMPLATE("Synth Table automation stores initialized state and resets "
                   "on transport start",
                   Synth, StackInstrument, ChiptuneInstrument) {
  Synth instrument;
  TableSaveState state;
  instrument.GetTableState(state);
  CHECK(state.position_[0] == 0);
  CHECK(state.position_[1] == 0);
  CHECK(state.position_[2] == 0);
  CHECK(state.hopCount_[15][2] == 0);
  state.position_[1] = 7;
  state.hopCount_[3][1] = 2;
  instrument.SetTableState(state);
  TableSaveState restored;
  instrument.GetTableState(restored);
  CHECK(restored.position_[1] == 7);
  CHECK(restored.hopCount_[3][1] == 2);
  instrument.OnStart();
  instrument.GetTableState(restored);
  CHECK(restored.position_[1] == 0);
  CHECK(restored.hopCount_[3][1] == 0);
}

TEST_CASE("Chiptune optional fields display the disabled marker and step back "
          "to their minimum") {
  ChiptuneInstrument instrument;
  for (const std::uint8_t index : {3, 5, 12}) {
    const auto descriptor =
        ui2::Ui2InstrumentFieldParameter(IT_CHIPTUNE, index);
    std::array<char, 16> text{};
    ui2::Ui2FormatInstrumentParameter(descriptor, VAR_OFF, 0, nullptr,
                                      text.data(), text.size());
    CHECK(std::string_view(text.data()) == "--");
    CHECK(ui2::Ui2AdjustInstrumentParameter(
              descriptor, VAR_OFF, ui2::Ui2InstrumentValueDirection::Right) ==
          descriptor.minimum);
    CHECK(ui2::Ui2AdjustInstrumentParameter(
              descriptor, descriptor.minimum,
              ui2::Ui2InstrumentValueDirection::Left) == VAR_OFF);
  }
}
