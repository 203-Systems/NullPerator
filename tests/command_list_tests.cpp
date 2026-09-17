#include "Application/Instruments/CommandList.h"
#include "doctest/doctest.h"

#include <cstdint>

TEST_CASE("Command coarse increments reach and saturate at VOL") {
  auto command = FourCC(FourCC::InstrumentCommandNone);
  for (unsigned step = 0; step < 64; ++step)
    command = CommandList::GetNextAlpha(command);
  CHECK(command == FourCC::InstrumentCommandVolume);
  CHECK(CommandList::GetNextAlpha(FourCC::InstrumentCommandTempo) ==
        FourCC::InstrumentCommandVelocity);
  CHECK(CommandList::GetNextAlpha(FourCC::InstrumentCommandVelocity) ==
        FourCC::InstrumentCommandVolume);
  CHECK(CommandList::GetNextAlpha(command) == command);
  CHECK(CommandList::GetNext(FourCC::InstrumentCommandVelocity) ==
        FourCC::InstrumentCommandVibrato);
  CHECK(CommandList::GetPrev(command) == FourCC::InstrumentCommandVibrato);
}

TEST_CASE("FX grid moves by row and preserves boundaries") {
  CHECK(CommandList::MoveGrid(FourCC::InstrumentCommandArpeggiator, 0, 1,
                              false) == FourCC::InstrumentCommandDelay);
  CHECK(CommandList::MoveGrid(FourCC::InstrumentCommandArpeggiator, -1, 0, false) == FourCC::InstrumentCommandNone);
  CHECK(CommandList::MoveGrid(FourCC::InstrumentCommandNone, -1, 0, false) == FourCC::InstrumentCommandNone);
  CHECK(CommandList::MoveGrid(FourCC::InstrumentCommandVolume, 0, 1, false) == FourCC::InstrumentCommandVolume);
  CHECK(CommandList::MoveGrid(FourCC::InstrumentCommandStop, 1, 0, true) == FourCC::InstrumentCommandTempo);
  CHECK(CommandList::MoveGrid(FourCC::InstrumentCommandStop, 1, 0, false) == FourCC::InstrumentCommandTable);
}

#include "Foundation/Types/FxCommands.h"
#include <string>

TEST_CASE("FX directory has four fixed groups for every instrument") {
  const std::string standard =
      "--- ARP CSH DLY GOF GRV HOP KIL LEG PAN PFT PSL RTG TBL TPO VIB VOL";
  const std::string tableStandard =
      "--- ARP CSH GOF GRV HOP IRT KIL LEG PAN PFT PSL RTG STP VIB VOL";
  const auto names = [](const fx::List &list, fx::Group group) {
    std::string result;
    for (int i = list.Begin(group); i < list.End(group); ++i) {
      if (i != list.Begin(group))
        result += ' ';
      result += list[i].name;
    }
    return result;
  };
  for (const auto instrument :
       {fx::Instrument::Sample, fx::Instrument::Midi, fx::Instrument::Sid,
        fx::Instrument::Opal, fx::Instrument::Drum, fx::Instrument::Stack,
        fx::Instrument::Chiptune, fx::Instrument::None,
        fx::Instrument::Unknown}) {
    for (const bool table : {false, true}) {
      const auto list = fx::BuildList({instrument, table});
      CHECK(list.count == (table ? 29 : 30));
      CHECK(list.Columns() == 6);
      CHECK(names(list, fx::Group::Standard) ==
            (table ? tableStandard : standard));
      CHECK(names(list, fx::Group::Sample) == "FCT FLT FRS LOF POF");
      CHECK(names(list, fx::Group::Midi) == "MCC MCH MPC VEL");
      CHECK(names(list, fx::Group::Synth) == "CHB CHD CHU SIP");
      // Browsing an unsupported command cannot move or duplicate any entries.
      CHECK(fx::BuildList({instrument, table}, "MCC").indices == list.indices);
      CHECK(fx::BuildList({instrument, table}, "SIP").indices == list.indices);
      for (std::size_t i = 0; i < list.count; ++i)
        CHECK(list[i].id != FourCC::Default);
    }
  }
  CHECK(fx::Available(FourCC::InstrumentCommandVibrato,
                      {fx::Instrument::Sample}));
  CHECK_FALSE(
      fx::Available(FourCC::InstrumentCommandVibrato, {fx::Instrument::Sid}));
  CHECK(fx::Available(FourCC::InstrumentCommandGateOff, {fx::Instrument::Sid}));
  CHECK_FALSE(fx::Available(FourCC::InstrumentCommandGateOff,
                            {fx::Instrument::Sample}));
}

TEST_CASE("SIP parameter descriptions follow the actual engine") {
  const auto *stack = fx::InstrumentParameter(fx::Instrument::Stack, 3);
  const auto *chip = fx::InstrumentParameter(fx::Instrument::Chiptune, 3);
  REQUIRE(stack != nullptr);
  REQUIRE(chip != nullptr);
  CHECK(stack->name == "Attack");
  CHECK(chip->name == "Noise Burst");
  CHECK(fx::InstrumentParameter(fx::Instrument::Stack, 8)->range == "00-0C");
  CHECK(fx::InstrumentParameter(fx::Instrument::Chiptune, 4)->range == "00-22");
  CHECK(fx::InstrumentParameter(fx::Instrument::Stack, 10) == nullptr);
  CHECK(fx::InstrumentParameter(fx::Instrument::Chiptune, 12) == nullptr);
  CHECK(fx::InstrumentParameter(fx::Instrument::Chiptune, 255) == nullptr);
  CHECK(fx::InstrumentParameter(fx::Instrument::Sample, 3) == nullptr);
  CHECK(fx::InstrumentParameter(fx::Instrument::Unknown, 3) == nullptr);
}

TEST_CASE("Six-row FX navigation reaches every entry without wrapping or "
          "duplicating entries") {
  for (const auto instrument :
       {fx::Instrument::Sample, fx::Instrument::Midi, fx::Instrument::Sid,
        fx::Instrument::Opal, fx::Instrument::Drum, fx::Instrument::Stack,
        fx::Instrument::Unknown, fx::Instrument::None,
        fx::Instrument::Chiptune}) {
    for (const bool table : {false, true}) {
      const fx::Context context{instrument, table};
      for (const auto &original : fx::commands) {
        const auto list = fx::BuildList(context, original.name);
        std::array<bool, 256> reached{};
        reached[original.id] = true;
        // Repeatedly expand reachable choices; even the shortest columns must
        // be traversable, including unsupported Standard effects.
        for (std::size_t pass = 0; pass < list.count; ++pass) {
          for (std::size_t i = 0; i < list.count; ++i) {
            if (!reached[list[i].id])
              continue;
            for (const auto direction :
                 {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
              const auto next =
                  fx::Move(list[i].id, direction.first, direction.second,
                           context, original.name);
              const auto *entry = fx::Find(next.c_str());
              REQUIRE(entry != nullptr);
              CHECK((fx::Available(next, context) || next == original.id ||
                     entry->common == fx::Neither));
              reached[static_cast<std::uint8_t>(next)] = true;
            }
            if (list.Row(i) == 0)
              CHECK(fx::Move(list[i].id, 0, -1, context, original.name) ==
                    list[i].id);
            if (list.Column(i) == 0)
              CHECK(fx::Move(list[i].id, -1, 0, context, original.name) ==
                    list[i].id);
          }
        }
        for (std::size_t i = 0; i < list.count; ++i) {
          CHECK(reached[list[i].id]);
          for (std::size_t j = i + 1; j < list.count; ++j)
            CHECK(list[i].id != list[j].id);
        }
        CHECK(list[0].name == "---");
      }
    }
  }
  const fx::Context sample{fx::Instrument::Sample};
  CHECK(fx::Move(FourCC::InstrumentCommandNone, 0, 1, sample) ==
        FourCC::InstrumentCommandArpeggiator);
  CHECK(fx::Move(FourCC::InstrumentCommandNone, 1, 0, sample) ==
        FourCC::InstrumentCommandHop);
  CHECK(fx::Move(FourCC::InstrumentCommandTempo, 1, 0, sample) ==
        FourCC::InstrumentCommandFilterResonance);
  CHECK(fx::Move(FourCC::InstrumentCommandMidiCC, 0, 1, sample, "MCC") ==
        FourCC::InstrumentCommandMidiChord);
  CHECK(fx::Move(FourCC::InstrumentCommandNone, 0, -1, sample, "MCC") ==
        FourCC::InstrumentCommandNone);
}
