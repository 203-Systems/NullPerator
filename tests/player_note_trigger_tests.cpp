#include "Application/Instruments/ChiptuneInstrument.h"
#include "Application/Instruments/DrumInstrument.h"
#include "Application/Instruments/StackInstrument.h"
#include "Application/Player/PhraseNoteTrigger.h"
#include "doctest/doctest.h"

#include <array>
#include <vector>

namespace {
constexpr auto delayCommand = FourCC::InstrumentCommandDelay;
constexpr auto volumeCommand = FourCC::InstrumentCommandVolume;
constexpr auto noCommand = FourCC::InstrumentCommandNone;

template <typename Synth> auto Render(Synth &instrument) {
  std::array<fixed, 512> audio{};
  instrument.Render(0, audio.data(), audio.size() / 2, false);
  return audio;
}

void ScheduleVolume(PhraseNoteTrigger &trigger, uchar note, ushort delay,
                    ushort volume, bool delayFirst) {
  trigger.Schedule(note, delayFirst ? delayCommand : volumeCommand,
                   delayFirst ? delay : volume,
                   delayFirst ? volumeCommand : delayCommand,
                   delayFirst ? volume : delay);
}
} // namespace

TEST_CASE_TEMPLATE("delayed phrase VOL leaves the old voice alone and applies "
                   "to the newly started synth",
                   Synth, StackInstrument, ChiptuneInstrument, DrumInstrument) {
  for (const bool delayFirst : {false, true}) {
    for (const ushort volume : {0x00, 0x40, 0xFF}) {
      CAPTURE(delayFirst);
      CAPTURE(volume);
      Synth actual, expected;
      REQUIRE(actual.Start(0, 48));
      REQUIRE(expected.Start(0, 48));
      actual.ProcessCommand(0, volumeCommand, 0x20);
      expected.ProcessCommand(0, volumeCommand, 0x20);

      PhraseNoteTrigger trigger;
      ScheduleVolume(trigger, 60, 1, volume, delayFirst);
      const auto processRowVolume = [&]() {
        if (!trigger.Defers(volumeCommand))
          actual.ProcessCommand(0, volumeCommand, volume);
      };
      int starts = 0;
      int volumes = 0;
      const auto tick = [&]() {
        trigger.Tick(
            [&]() {
              ++starts;
              actual.Stop(0);
              REQUIRE(actual.Start(0, 60));
            },
            [&](ushort value) {
              ++volumes;
              actual.ProcessCommand(0, volumeCommand, value);
            });
      };

      // Transport startup can process row commands before the first tick.
      processRowVolume();
      CHECK(Render(actual) == Render(expected));
      tick(); // row boundary: DLY 1 must keep the old voice for one tick
      processRowVolume();
      CHECK(starts == 0);
      CHECK(volumes == 0);
      CHECK(Render(actual) == Render(expected));

      tick();
      CHECK(starts == 1);
      CHECK(volumes == 1);
      expected.Stop(0);
      REQUIRE(expected.Start(0, 60));
      expected.ProcessCommand(0, volumeCommand, volume);
      CHECK(Render(actual) == Render(expected));
      for (int i = 0; i < 16; ++i)
        tick();
      CHECK(starts == 1);
      CHECK(volumes == 1);
    }
  }
}

TEST_CASE("delayed first notes keep the full VOL word and callback order") {
  for (const bool delayFirst : {false, true}) {
    for (const ushort delay : {1, 5, 15, 0xABF1}) {
      CAPTURE(delay);
      PhraseNoteTrigger trigger;
      ScheduleVolume(trigger, HIGHEST_NOTE, delay, 0x1280, delayFirst);
      std::vector<int> events;
      const auto tick = [&]() {
        trigger.Tick([&]() { events.push_back(-1); },
                     [&](ushort volume) { events.push_back(volume); });
      };
      for (int i = 0; i < (delay & 0x0F); ++i) {
        REQUIRE(trigger.Defers(volumeCommand));
        tick();
        CHECK(events.empty());
      }
      tick();
      CHECK(events == std::vector<int>{-1, 0x1280});
      CHECK_FALSE(trigger.Defers(volumeCommand));
    }
  }
}

TEST_CASE("a delayed instrument change applies VOL to the new instrument") {
  StackInstrument previous;
  ChiptuneInstrument next, expected;
  REQUIRE(previous.Start(0, 48));
  I_Instrument *current = &previous;
  PhraseNoteTrigger trigger;
  ScheduleVolume(trigger, 60, 1, 0x40, true);
  const auto tick = [&]() {
    trigger.Tick(
        [&]() {
          current->Stop(0);
          current = &next;
          REQUIRE(current->Start(0, 60));
        },
        [&](ushort volume) {
          current->ProcessCommand(0, volumeCommand, volume);
        });
  };
  tick();
  CHECK(current == &previous);
  tick();
  CHECK(current == &next);
  REQUIRE(expected.Start(0, 60));
  expected.ProcessCommand(0, volumeCommand, 0x40);
  CHECK(Render(next) == Render(expected));
}

TEST_CASE("ordinary VOL and other row effects retain their row timing") {
  PhraseNoteTrigger trigger;
  for (const uchar note : {uchar(60), uchar(NOTE_OFF), uchar(0xFF)}) {
    for (const ushort delay : {0, 1}) {
      ScheduleVolume(trigger, note, delay, 0x40, true);
      CHECK(trigger.Defers(volumeCommand) == (note == 60 && delay != 0));
      for (const auto command : {FourCC::InstrumentCommandTempo,
                                 FourCC::InstrumentCommandGroove,
                                 FourCC::InstrumentCommandKill,
                                 FourCC::InstrumentCommandTable,
                                 FourCC::InstrumentCommandPan})
        CHECK_FALSE(trigger.Defers(command));
    }
  }
  trigger.Schedule(60, volumeCommand, 0x40, noCommand, 0);
  CHECK_FALSE(trigger.Defers(volumeCommand));
  int starts = 0;
  trigger.Tick([&]() { ++starts; },
               [&](ushort) { FAIL("unexpected deferred VOL"); });
  CHECK(starts == 1);
}

TEST_CASE("the second DLY column still wins and DLY zero is immediate") {
  PhraseNoteTrigger trigger;
  for (const ushort delay : {0, 1, 5}) {
    trigger.Schedule(60, delayCommand, 15, delayCommand, delay);
    int starts = 0;
    for (int tick = 0; tick <= delay; ++tick) {
      trigger.Tick([&]() { ++starts; },
                   [&](ushort) { FAIL("unexpected deferred VOL"); });
      CHECK(starts == (tick == delay ? 1 : 0));
    }
  }
}

TEST_CASE("new rows and transport reset cancel an unfinished delayed VOL") {
  PhraseNoteTrigger trigger;
  const auto noStart = []() { FAIL("cancelled note started"); };
  const auto noVolume = [](ushort) { FAIL("stale deferred VOL"); };

  ScheduleVolume(trigger, 60, 15, 0x40, true);
  trigger.Tick(noStart, noVolume);
  trigger.Reset();
  for (int i = 0; i < 20; ++i)
    trigger.Tick(noStart, noVolume);

  ScheduleVolume(trigger, 60, 15, 0x40, true);
  trigger.Tick(noStart, noVolume);
  trigger.Schedule(62, noCommand, 0, noCommand, 0);
  CHECK_FALSE(trigger.Defers(volumeCommand));
  int starts = 0;
  trigger.Tick([&]() { ++starts; }, noVolume);
  CHECK(starts == 1);

  ScheduleVolume(trigger, 60, 15, 0x40, true);
  trigger.Tick(noStart, noVolume);
  ScheduleVolume(trigger, 62, 1, 0x80, false);
  trigger.Tick(noStart, noVolume);
  trigger.Tick([&]() { ++starts; }, [](ushort volume) { CHECK(volume == 0x80); });
  CHECK(starts == 2);
}

TEST_CASE("delayed volume state is independent for each track") {
  std::array<PhraseNoteTrigger, 2> triggers;
  ScheduleVolume(triggers[0], 60, 1, 0x20, true);
  ScheduleVolume(triggers[1], 60, 3, 0x80, false);
  std::array<int, 2> starts{};
  std::array<ushort, 2> volumes{};
  for (int tick = 0; tick < 4; ++tick) {
    for (int track = 0; track < 2; ++track) {
      triggers[track].Tick([&]() { ++starts[track]; },
                           [&](ushort volume) { volumes[track] = volume; });
    }
    CHECK(starts[0] == (tick >= 1 ? 1 : 0));
    CHECK(starts[1] == (tick >= 3 ? 1 : 0));
  }
  CHECK(volumes == std::array<ushort, 2>{0x20, 0x80});
}
