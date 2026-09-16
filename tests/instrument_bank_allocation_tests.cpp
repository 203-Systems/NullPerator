#include "Application/Instruments/InstrumentBank.h"
#include "Foundation/Variables/StringVariable.h"
#include "doctest/doctest.h"
#include <array>
#include <cstdlib>
#include <set>

namespace {
std::set<void *> live;
int failAfter = -1;
void *Allocate(std::size_t size) {
  if (failAfter == 0)
    return nullptr;
  if (failAfter > 0)
    --failAfter;
  void *ptr = std::malloc(size);
  if (ptr)
    live.insert(ptr);
  return ptr;
}
void Free(void *ptr) {
  CHECK(live.erase(ptr) == 1);
  std::free(ptr);
}
InstrumentBank::Allocator allocator{Allocate, Free};
} // namespace

TEST_CASE(
    "Real bank allocates all 64 slots as any type and releases on reset") {
  for (int type = IT_SAMPLE; type < IT_LAST; ++type) {
    CAPTURE(type);
    const unsigned poolCount = 0;
    InstrumentBank bank(allocator);
    CHECK(live.empty());
    for (int round = 0; round < 3; ++round) {
      for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot) {
        REQUIRE(bank.GetNextAndAssignID(static_cast<InstrumentType>(type),
                                        slot) == slot);
        CHECK(bank.GetInstrument(slot)->GetType() == type);
      }
      CHECK(live.size() == MAX_INSTRUMENT_COUNT + poolCount);
      CHECK(bank.GetNextFreeInstrumentSlotId() == NO_MORE_INSTRUMENT);
      I_Instrument *first = bank.GetInstrument(0);
      CHECK(bank.Clone(0) == NO_MORE_INSTRUMENT);
      CHECK(bank.GetInstrument(0) == first);
      InstrumentBank::Replacement replacement;
      REQUIRE(bank.BeginReplacement(0, static_cast<InstrumentType>(type),
                                    replacement));
      CHECK(live.size() == MAX_INSTRUMENT_COUNT + poolCount + 1);
      REQUIRE(replacement.Commit());
      CHECK(live.size() == MAX_INSTRUMENT_COUNT + poolCount);
      bank.Reset();
      CHECK(live.empty());
    }
  }
}

TEST_CASE("Failed and cancelled replacements preserve original instruments") {
  {
    InstrumentBank bank(allocator);
    REQUIRE(bank.GetNextAndAssignID(IT_CHIPTUNE, 0) == 0);
    auto *original = bank.GetInstrument(0);
    original->FindVariable(FourCC::ChiptuneVolume)->SetInt(71);
    InstrumentBank::Replacement replacement;
    failAfter = 0;
    CHECK_FALSE(bank.BeginReplacement(0, IT_DRUM, replacement));
    CHECK(bank.GetInstrument(0) == original);
    CHECK(bank.GetNextAndAssignID(IT_DRUM, 1) == NO_MORE_INSTRUMENT);
    failAfter = -1;
    REQUIRE(bank.BeginReplacement(0, IT_DRUM, replacement));
    replacement.Cancel();
    CHECK(bank.GetInstrument(0) == original);
    CHECK(original->FindVariable(FourCC::ChiptuneVolume)->GetInt() == 71);
    CHECK(live.size() == 1);
    bank.releaseInstrument(1000);
    CHECK(bank.Clone(1000) == NO_MORE_INSTRUMENT);
    CHECK(bank.GetInstrument(-1)->GetType() == IT_NONE);
  }
  CHECK(live.empty());
}

TEST_CASE_TEMPLATE(
    "Shared track voices match isolated rendering across preset swaps", Synth,
    ChiptuneInstrument, DrumInstrument, StackInstrument) {
  InstrumentBank bank(allocator);
  Synth reference;
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot)
    REQUIRE(bank.GetNextAndAssignID(reference.GetType(), slot) == slot);
  std::array<fixed, 512> actual{}, expected{};
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot) {
    auto *current = bank.GetInstrument(slot);
    const FourCC parameter =
        reference.GetType() == IT_CHIPTUNE ? FourCC::ChiptuneVolume
        : reference.GetType() == IT_DRUM   ? FourCC::DrumCharacter
                                           : FourCC::StackSpread;
    current->FindVariable(parameter)->SetInt(64 + slot);
    reference.FindVariable(parameter)->SetInt(64 + slot);
    for (int channel = 0; channel < SONG_CHANNEL_COUNT; ++channel) {
      reference.Stop(channel);
      const auto note = static_cast<unsigned char>(48 + slot % 24);
      REQUIRE(reference.Start(channel, note));
      REQUIRE(current->Start(channel, note));
      if (slot > 0) {
        auto *old = bank.GetInstrument(slot - 1);
        old->Stop(channel);
        old->ProcessCommand(channel, FourCC::InstrumentCommandKill, 0);
        CHECK_FALSE(old->Render(channel, actual.data(), 256, false));
      }
      REQUIRE(reference.Render(channel, expected.data(), 256, false));
      REQUIRE(current->Render(channel, actual.data(), 256, false));
      CHECK(actual == expected);
    }
  }
  bank.Reset();
  CHECK(live.empty());
}

TEST_CASE("Allocation failure halfway through staging releases all candidates") {
  InstrumentBank bank(allocator);
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot)
    REQUIRE(bank.GetNextAndAssignID(IT_CHIPTUNE, slot) == slot);
  std::array<I_Instrument *, MAX_INSTRUMENT_COUNT> original{};
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot)
    original[slot] = bank.GetInstrument(slot);
  {
    std::array<InstrumentBank::Replacement, MAX_INSTRUMENT_COUNT> staged;
    failAfter = 7;
    for (int slot = 0; slot < 7; ++slot)
      CHECK(bank.BeginReplacement(slot, IT_DRUM, staged[slot]));
    CHECK_FALSE(bank.BeginReplacement(7, IT_DRUM, staged[7]));
    failAfter = -1;
    CHECK(live.size() == MAX_INSTRUMENT_COUNT + 7);
  }
  CHECK(live.size() == MAX_INSTRUMENT_COUNT);
  for (int slot = 0; slot < MAX_INSTRUMENT_COUNT; ++slot)
    CHECK(bank.GetInstrument(slot) == original[slot]);
  bank.Reset();
  CHECK(live.empty());
}

TEST_CASE("Stale replacement cannot overwrite a reused empty slot") {
  InstrumentBank bank(allocator);
  InstrumentBank::Replacement replacement;
  REQUIRE(bank.BeginReplacement(0, IT_CHIPTUNE, replacement));
  REQUIRE(bank.GetNextAndAssignID(IT_DRUM, 0) == 0);
  bank.releaseInstrument(0);
  // Both old and current pointers are the none sentinel: generation must catch
  // this ABA case even though a pointer comparison would accept it.
  CHECK_FALSE(replacement.Commit());
  CHECK(bank.GetInstrument(0)->GetType() == IT_NONE);
  replacement.Cancel();
  CHECK(live.empty());
}

TEST_CASE(
    "Cloning a synth shares definitions but not editable parameter values") {
  InstrumentBank bank(allocator);
  REQUIRE(bank.GetNextAndAssignID(IT_CHIPTUNE, 0) == 0);
  bank.GetInstrument(0)->SetName("Original");
  auto *source = bank.GetInstrument(0)->FindVariable(FourCC::ChiptuneVolume);
  source->SetInt(71);
  REQUIRE(bank.Clone(0) == 1);
  auto *copy = bank.GetInstrument(1)->FindVariable(FourCC::ChiptuneVolume);
  CHECK(copy->GetInt() == 71);
  CHECK(bank.GetInstrument(1)->GetUserSetName() == "Original");
  copy->SetInt(20);
  CHECK(source->GetInt() == 71);
  bank.Reset();
  CHECK(live.empty());
}

TEST_CASE(
    "Shared parameter schemas keep values defaults and lists independent") {
  static const char *const choices[]{"ONE", "TWO"};
  static const Variable::Descriptor schema(FourCC::ChiptuneWave, choices, 2, 0);
  Variable first(schema), second(schema);
  first.SetString("TWO");
  CHECK(second.GetInt() == 0);
  second.CopyFrom(first);
  CHECK(second.GetInt() == 1);
  first.Reset();
  CHECK(first.GetInt() == 0);
  CHECK(second.GetInt() == 1);
  CHECK(second.GetListPointer() == choices);
  OwnedVariable destination(FourCC::ChiptuneVolume, 20);
  OwnedVariable source(FourCC::ChiptuneTranspose, 3);
  source.SetInt(10);
  destination.CopyFrom(source);
  CHECK(destination.GetID() == FourCC::ChiptuneVolume);
  CHECK(destination.GetInt() == 10);
  destination.Reset();
  CHECK(destination.GetInt() == 20);
  StringVariable<> a(FourCC::InstrumentName, "original"),
      b(FourCC::InstrumentName, "copy");
  a.CopyFrom(b);
  CHECK(a.GetString() == "copy");
  a.Reset();
  CHECK(a.GetString() == "original");
}
