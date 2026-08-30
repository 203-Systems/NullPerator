#include "doctest/doctest.h"

#include "Foundation/Types/Types.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/Utils/FourCCSerialization.h"
#include "Foundation/Variables/VariableContainer.h"
#include "etl/vector.h"

#include <array>
#include <cstring>

TEST_CASE("FourCC stable enum values") {
  CHECK(FourCC::InstrumentCommandArpeggiator == 0);
  CHECK(FourCC::VarTempo == 33);
  CHECK(FourCC::VarScaleRoot == 162);
}

TEST_CASE("SID note lookup accepts exactly its 96-entry hardware range") {
  CHECK_FALSE(SIDInstrument::IsPlayableNote(0U));
  CHECK_FALSE(SIDInstrument::IsPlayableNote(23U));
  CHECK(SIDInstrument::IsPlayableNote(24U));
  CHECK(SIDInstrument::IsPlayableNote(119U));
  CHECK_FALSE(SIDInstrument::IsPlayableNote(120U));
  CHECK_FALSE(SIDInstrument::IsPlayableNote(127U));
  CHECK_FALSE(SIDInstrument::IsPlayableNote(255U));
}

namespace {

int ClassifyHighFourCC(FourCC id) {
  switch (id) {
  case FourCC::VarOutputVolume:
    return 1;
  case FourCC::VarUITextCase:
    return 2;
  default:
    return 0;
  }
}

} // namespace

TEST_CASE("FourCC wrapper preserves identifiers at and above bit seven") {
  const FourCC outputVolume = FourCC::VarOutputVolume;
  const FourCC textCase = FourCC::VarUITextCase;
  const FourCC invalid = FourCC::Default;

  CHECK(outputVolume.get_value() == 184U);
  CHECK(outputVolume.get_enum() == FourCC::VarOutputVolume);
  CHECK(outputVolume == FourCC::VarOutputVolume);
  CHECK_FALSE(outputVolume == FourCC::VarTempo);
  CHECK(textCase == FourCC::VarUITextCase);
  CHECK(invalid.get_value() == 255U);

  CHECK(ClassifyHighFourCC(outputVolume) == 1);
  CHECK(ClassifyHighFourCC(textCase) == 2);
  CHECK(ClassifyHighFourCC(FourCC::VarTempo) == 0);
}

TEST_CASE("VariableContainer finds variables with high FourCC identifiers") {
  Variable outputVolume(FourCC::VarOutputVolume, 40);
  Variable textCase(FourCC::VarUITextCase, 0);
  etl::vector<Variable *, 2> variables;
  variables.push_back(&outputVolume);
  variables.push_back(&textCase);
  VariableContainer container(&variables);

  CHECK(container.FindVariable(FourCC::VarOutputVolume) == &outputVolume);
  CHECK(container.FindVariable(FourCC::VarUITextCase) == &textCase);
  CHECK(container.FindVariable(FourCC::VarTempo) == nullptr);
}

TEST_CASE("float variables format their integer part without float printf") {
  Variable value(FourCC::VarTempo, 12.75F);
  CHECK(std::strcmp(value.GetString().c_str(), "12") == 0);

  value.SetFloat(-3.5F, false);
  CHECK(std::strcmp(value.GetString().c_str(), "-3") == 0);
}

TEST_CASE("FourCC command persistence is byte-sized and platform stable") {
  CHECK(sizeof(FourCC) == 1U);
  CHECK(sizeof(FourCC::enum_type) == 1U);

  std::array<FourCC, 4> restored{};
  const std::array<std::uint8_t, 4> packed = {
      FourCC::InstrumentCommandArpeggiator,
      FourCC::InstrumentCommandKill,
      FourCC::InstrumentCommandFilterCut,
      FourCC::InstrumentCommandMidiChord};
  FourCCSerialization::DecodeCommands(packed, restored.data(),
                                      restored.size());
  CHECK(std::strcmp(restored[0].c_str(), "ARP") == 0);
  CHECK(std::strcmp(restored[1].c_str(), "KIL") == 0);
  CHECK(std::strcmp(restored[2].c_str(), "FCT") == 0);
  CHECK(std::strcmp(restored[3].c_str(), "MCH") == 0);
}

TEST_CASE("legacy Web LE32 command buffers restore real FX commands") {
  const std::array<std::uint8_t, 12> webLe32 = {
      FourCC::InstrumentCommandKill, 0, 0, 0,
      FourCC::InstrumentCommandFilterResonance, 0, 0, 0,
      FourCC::InstrumentCommandTable, 0, 0, 0};
  std::array<FourCC, 3> restored{};
  FourCCSerialization::DecodeCommands(webLe32, restored.data(),
                                      restored.size());
  CHECK(std::strcmp(restored[0].c_str(), "KIL") == 0);
  CHECK(std::strcmp(restored[1].c_str(), "FRS") == 0);
  CHECK(std::strcmp(restored[2].c_str(), "TBL") == 0);
}

TEST_CASE("FourCC decoder accepts LE16 and rejects malformed words") {
  const std::array<std::uint8_t, 8> le16 = {
      FourCC::InstrumentCommandKill, 0,
      FourCC::InstrumentCommandFilterResonance, 0,
      0xFE, 0,
      FourCC::InstrumentCommandTable, 1};
  std::array<FourCC, 4> restored{};
  FourCCSerialization::DecodeCommands(le16, restored.data(), restored.size());
  CHECK(restored[0] == FourCC::InstrumentCommandKill);
  CHECK(restored[1] == FourCC::InstrumentCommandFilterResonance);
  CHECK(restored[2] == FourCC::InstrumentCommandNone);
  CHECK(restored[3] == FourCC::InstrumentCommandNone);
}

TEST_CASE("FourCC streaming decoder handles odd and even ABI boundaries") {
  constexpr std::size_t maxCommands = 33U;
  std::array<FourCC, maxCommands> source{};
  constexpr FourCC::enum_type pattern[] = {
      FourCC::InstrumentCommandArpeggiator,
      FourCC::InstrumentCommandKill,
      FourCC::InstrumentCommandFilterResonance,
      FourCC::InstrumentCommandTable,
      FourCC::InstrumentCommandMidiChord,
      FourCC::InstrumentCommandNone};
  for (std::size_t index = 0; index < source.size(); ++index)
    source[index] = pattern[index % std::size(pattern)];

  for (std::size_t count = 1U; count <= maxCommands; ++count) {
    for (const std::size_t stride : {1U, 2U, 4U}) {
      std::array<std::uint8_t, maxCommands * 4U> encoded{};
      for (std::size_t index = 0; index < count; ++index)
        encoded[index * stride] =
            static_cast<std::uint8_t>(source[index].get_value());

      std::array<FourCC, maxCommands> restored{};
      FourCCSerialization::DecodeCommands(
          std::span<const std::uint8_t>(encoded.data(), count * stride),
          restored.data(), count);
      for (std::size_t index = 0; index < count; ++index)
        CHECK(restored[index] == source[index]);
    }
  }
}
