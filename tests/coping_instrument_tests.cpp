#include "Application/Instruments/ChiptuneInstrument.h"
#include "Application/Instruments/DrumInstrument.h"
#include "Application/Instruments/InstrumentBankRestorePolicy.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/StackInstrument.h"
#include "Application/Player/TablePlayback.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Application/UI2/Ui2NotePresentation.h"
#include "doctest/doctest.h"
#include <algorithm>
#include <array>

namespace {
// Table playback may be the first user of Groove in this host process. Do not
// leave it initialized before the persistence tests install their service.
struct ScopedTableGroove {
  bool existed = etl::singleton<Groove>::is_valid();
  ~ScopedTableGroove() {
    if (!existed && etl::singleton<Groove>::is_valid())
      etl::singleton<Groove>::destroy();
  }
};
} // namespace

TEST_CASE_TEMPLATE("Table KIL schedules voice termination without dispatching "
                   "an immediate kill",
                   Synth, DrumInstrument, StackInstrument, ChiptuneInstrument) {
  ScopedTableGroove groove;
  for (int column = 0; column < TABLE_COLUMNS; ++column) {
    for (const bool automated : {false, true}) {
      Synth instrument;
      if (automated && instrument.GetType() == IT_DRUM)
        continue; // Drum explicitly has no automation-table state.
      REQUIRE(instrument.Start(0, 60));
      Table table;
      FourCC *commands[] = {table.cmd1_, table.cmd2_, table.cmd3_};
      ushort *parameters[] = {table.param1_, table.param2_, table.param3_};
      commands[column][0] = FourCC::InstrumentCommandKill;
      parameters[column][0] = 5;
      commands[(column + 1) % TABLE_COLUMNS][0] =
          FourCC::InstrumentCommandVolume;
      parameters[(column + 1) % TABLE_COLUMNS][0] = 0x80;
      TablePlayback playback;
      playback.Init(0);
      playback.Start(&instrument, table, automated);
      TablePlayerChange change{0, -1};
      playback.ProcessStep(change);
      CHECK(change.timeToLive_ == 6);
      std::array<fixed, 256> buffer{};
      CHECK(instrument.Render(0, buffer.data(), 128, false));
      CHECK(std::any_of(buffer.begin(), buffer.end(),
                        [](fixed x) { return x != 0; }));
      instrument.Stop(0);
    }
  }
}

TEST_CASE("Table HOP uses aa as the repeat count and only the last digit as "
          "its target") {
  Table table;
  table.cmd1_[0] = FourCC::InstrumentCommandHop;
  table.param1_[0] = 0x02A5; // Two jumps to row 5; A is unused.
  table.cmd1_[5] = FourCC::InstrumentCommandHop;
  table.param1_[5] = 0x0000; // Return to the counted HOP.
  table.cmd1_[1] = FourCC::InstrumentCommandKill;
  table.param1_[1] = 0x007B; // Observable fall-through after two repeats.
  TablePlayback playback;
  playback.Init(0);
  for (int visit = 0; visit < 5; ++visit) {
    TablePlayerChange change{-1, -1};
    CHECK(playback.ProcessLocalCommand(0, table.cmd1_, table.param1_, change));
    CHECK(change.timeToLive_ == (visit == 4 ? 0x7C : -1));
  }
  table.param1_[0] =
      0x00A5; // Zero count keeps jumping without falling through.
  playback.Init(0);
  for (int visit = 0; visit < 20; ++visit) {
    TablePlayerChange change{-1, -1};
    CHECK(playback.ProcessLocalCommand(0, table.cmd1_, table.param1_, change));
    CHECK(change.timeToLive_ == -1);
  }
}

TEST_CASE("Table STP can stop safely from any command column") {
  ScopedTableGroove groove;
  for (int column = 0; column < TABLE_COLUMNS; ++column) {
    StackInstrument instrument;
    REQUIRE(instrument.Start(0, 60));
    Table table;
    FourCC *commands[] = {table.cmd1_, table.cmd2_, table.cmd3_};
    commands[column][0] = FourCC::InstrumentCommandStop;
    TablePlayback playback;
    playback.Init(0);
    playback.Start(&instrument, table, false);
    TablePlayerChange change{0, -1};
    playback.ProcessStep(change);
    CHECK(playback.GetTable() == nullptr);
    std::array<fixed, 256> buffer{};
    CHECK(instrument.Render(0, buffer.data(), 128, false));
  }
}

TEST_CASE(
    "Table MIDI KIL preserves the scheduled delay and still forwards VOL") {
  ScopedTableGroove groove;
  static MidiService service;
  MidiService::Install(&service);
  struct ObservedMidi : MidiInstrument {
    int kills = 0;
    int volumes = 0;
    void ProcessCommand(int channel, FourCC command, ushort value) override {
      if (command == FourCC::InstrumentCommandKill)
        ++kills;
      if (command == FourCC::InstrumentCommandVolume)
        ++volumes;
      MidiInstrument::ProcessCommand(channel, command, value);
    }
  } instrument;
  REQUIRE(instrument.Start(0, 60));
  Table table;
  table.cmd1_[0] = FourCC::InstrumentCommandKill;
  table.param1_[0] = 7;
  table.cmd2_[0] = FourCC::InstrumentCommandVolume;
  table.param2_[0] = 0xFF;
  TablePlayback playback;
  playback.Init(0);
  playback.Start(&instrument, table, false);
  TablePlayerChange change{0, -1};
  playback.ProcessStep(change);
  CHECK(change.timeToLive_ == 8);
  CHECK(instrument.kills == 0);
  CHECK(instrument.volumes == 1);
  instrument.Stop(0);
}

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

TEST_CASE("Every instrument type may use all 64 restore slots") {
  for (int type = IT_SAMPLE; type < IT_LAST; ++type) {
    InstrumentBankRestorePolicy policy;
    for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot)
      CHECK(policy.Reserve(slot, static_cast<InstrumentType>(type)));
    CHECK_FALSE(policy.Reserve(MAX_INSTRUMENT_COUNT,
                               static_cast<InstrumentType>(type)));
    CHECK_FALSE(policy.Reserve(0, static_cast<InstrumentType>(type)));
  }
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

TEST_CASE("Mixed instrument types share the logical slot limit") {
  InstrumentBankRestorePolicy policy;
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot)
    CHECK(policy.Reserve(slot, slot < 40 ? IT_CHIPTUNE : IT_SAMPLE));
  CHECK_FALSE(policy.Reserve(40, IT_DRUM));
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
