#include "doctest/doctest.h"

#include "Application/Instruments/OpalInstrumentParameterEncoding.h"
#include "Application/Instruments/MacroInstrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleRenderingParams.h"
#include "Application/Instruments/SIDInstrument.h"
#include "Application/UI2/Controllers/Ui2InstrumentLifecycleController.h"
#include "Application/UI2/Ui2InstrumentParameters.h"
#include "Application/UI2/Ui2InstrumentTableAllocation.h"
#include "Application/UI2/Ui2InstrumentTypeTransaction.h"
#include "Application/UI2/Ui2TransportPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

struct MacroInstrumentTestPeer {
  static std::size_t GainOffset(MacroInstrument &instrument) {
    const auto *object = reinterpret_cast<const std::byte *>(&instrument);
    const auto *gain = reinterpret_cast<const std::byte *>(&instrument.gain_lp_);
    return static_cast<std::size_t>(gain - object);
  }
};

namespace {
std::vector<MidiMessage> capturedMidiMessages;

class FakeMidiService final : public MidiService {};

void InstallFakeMidiService() {
  static FakeMidiService service;
  MidiService::Install(&service);
}
}

// This focused host target does not link a platform MidiService. Keep the
// production factory boundary while recording exactly what MidiInstrument
// asks the service to send.
MidiService::MidiService() = default;
MidiService::~MidiService() = default;
void MidiService::QueueMessage(MidiMessage &message) {
  capturedMidiMessages.push_back(message);
}
void MidiService::RegisterActiveChannel(uint8_t channel) { (void)channel; }
void MidiService::Update(Observable &observable, I_ObservableData *data) {
  (void)observable;
  (void)data;
}
void MidiService::updateActiveDevicesList(unsigned short config) {
  (void)config;
}

// The focused host binary does not link TablePlayback.cpp; provide its small
// value-state reset so the production SID lifecycle can be exercised directly.
void TableSaveState::Reset() {
  for (std::size_t row = 0U; row < TABLE_STEPS; ++row)
    for (std::size_t column = 0U; column < TABLE_COLUMNS; ++column)
      hopCount_[row][column] = 0U;
  for (std::size_t column = 0U; column < TABLE_COLUMNS; ++column)
    position_[column] = 0;
  groove_.groove_ = static_cast<unsigned char>(-1);
  groove_.position_ = 0U;
  groove_.ticks_ = 0U;
}

namespace {

ui2::Ui2InstrumentLifecycleCommand
Tap(ui2::Ui2InstrumentLifecycleController &controller,
    TrackerAction action) {
  const auto command = controller.Handle(action, true);
  controller.Handle(action, false);
  return command;
}

std::string_view Format(const ui2::Ui2InstrumentParameterDescriptor &field,
                        int current, int secondary = 0,
                        const char *text = nullptr) {
  static std::array<char, 32> output{};
  ui2::Ui2FormatInstrumentParameter(field, current, secondary, text,
                                    output.data(), output.size());
  return output.data();
}

enum class FakeType : std::uint8_t { None, Sample, Midi };
struct FakeInstrument {
  FakeType type = FakeType::Sample;
  int value = 73;
};

struct FakeVariable {
  bool modified = false;
  [[nodiscard]] bool IsModified() const { return modified; }
};

struct FakeInstrumentModificationState {
  std::array<FakeVariable *, 2> variables{};
  std::string_view name{};
  std::array<FakeVariable *, 2> *Variables() { return &variables; }
  std::string_view GetUserSetName() const { return name; }
};

struct FakeMidiInstrument {
  void SendProgramChange(int channel, int program) {
    ++callCount;
    lastChannel = channel;
    lastProgram = program;
  }
  int callCount = 0;
  int lastChannel = -1;
  int lastProgram = -2;
};

class FakeBank {
public:
  class Replacement {
  public:
    Replacement() = default;
    Replacement(const Replacement &) = delete;
    Replacement &operator=(const Replacement &) = delete;
    ~Replacement() { Cancel(); }

    bool Commit() {
      if (bank_ == nullptr || !bank_->commitAllowed_)
        return false;
      bank_->visible_ = candidate_;
      bank_->candidateInUse_ = false;
      bank_ = nullptr;
      candidate_ = nullptr;
      return true;
    }
    void Cancel() {
      if (bank_ != nullptr)
        bank_->candidateInUse_ = false;
      bank_ = nullptr;
      candidate_ = nullptr;
    }

  private:
    friend class FakeBank;
    FakeBank *bank_ = nullptr;
    FakeInstrument *candidate_ = nullptr;
  };

  bool BeginReplacement(unsigned short slot, FakeType type,
                        Replacement &replacement) {
    if (slot != 0U || allocationExhausted_ || candidateInUse_)
      return false;
    candidate_ = {.type = type, .value = 0};
    candidateInUse_ = true;
    replacement.bank_ = this;
    replacement.candidate_ = &candidate_;
    return true;
  }

  FakeInstrument original_{};
  FakeInstrument candidate_{};
  FakeInstrument *visible_ = &original_;
  bool allocationExhausted_ = false;
  bool commitAllowed_ = true;
  bool candidateInUse_ = false;
};

} // namespace

TEST_CASE("UI2 Instrument booleans use NO and YES labels") {
  constexpr auto descriptor = ui2::detail::Parameter(
      "AUTOMATION", FourCC::SampleInstrumentTableAutomation, 0, 1, 1, 1,
      0, 0, ui2::Ui2InstrumentValueFormat::Boolean);
  CHECK(Format(descriptor, 0) == "NO");
  CHECK(Format(descriptor, 1) == "YES");
  CHECK(ui2::Ui2AdjustInstrumentParameter(
            descriptor, 0, ui2::Ui2InstrumentValueDirection::Left) == 1);
  CHECK(ui2::Ui2AdjustInstrumentParameter(
            descriptor, 1, ui2::Ui2InstrumentValueDirection::Right) == 0);
}

TEST_CASE("OPAL output levels retain each operator keyscale") {
  constexpr OpalOutputLevelRegisters encoded =
      EncodeOpalOutputLevels(1, 0x17, 3, 0x05);
  CHECK(encoded.operator1 == 0x57U);
  CHECK(encoded.operator2 == 0xC5U);
}

TEST_CASE("OPAL channel control includes feedback and algorithm") {
  CHECK(EncodeOpalChannelControl(0, 0) == 0x30U);
  CHECK(EncodeOpalChannelControl(1, 5) == 0x3BU);
}

TEST_CASE("OPAL depth flags retain the UI tremolo and vibrato bit order") {
  CHECK(EncodeOpalDepthControl(0) == 0x00U);
  CHECK(EncodeOpalDepthControl(1) == 0x40U);
  CHECK(EncodeOpalDepthControl(2) == 0x80U);
  CHECK(EncodeOpalDepthControl(3) == 0xC0U);
}

TEST_CASE("SID render is inert before the first note starts") {
  alignas(SIDInstrument)
      std::array<std::byte, sizeof(SIDInstrument)> storage{};
  storage.fill(std::byte{0xFF});
  SIDInstrument *instrument =
      std::construct_at(reinterpret_cast<SIDInstrument *>(storage.data()), SID1);
  REQUIRE(instrument->Init());
  std::array<fixed, 8> buffer{};
  buffer.fill(123);

  CHECK_FALSE(instrument->Render(0, buffer.data(), 4, false));
  for (fixed sample : buffer)
    CHECK(sample == 123);
  std::destroy_at(instrument);
}

TEST_CASE("MIDI playback state is deterministic before the first note") {
  InstallFakeMidiService();
  alignas(MidiInstrument)
      std::array<std::byte, sizeof(MidiInstrument)> storage{};
  storage.fill(std::byte{0xFF});
  MidiInstrument *instrument =
      std::construct_at(reinterpret_cast<MidiInstrument *>(storage.data()));
  REQUIRE(instrument->Init());
  capturedMidiMessages.clear();

  instrument->Stop(0);
  CHECK(capturedMidiMessages.empty());

  std::array<fixed, 8> buffer{};
  CHECK_FALSE(instrument->Render(0, buffer.data(), 4, false));
  CHECK(capturedMidiMessages.empty());

  REQUIRE(instrument->Start(0, 60));
  instrument->Render(0, buffer.data(), 4, false);
  REQUIRE(capturedMidiMessages.size() == 1U);
  CHECK(capturedMidiMessages[0].status_ == MidiMessage::MIDI_NOTE_ON);
  CHECK(capturedMidiMessages[0].data1_ == 60U);
  CHECK(capturedMidiMessages[0].data2_ == INITIAL_NOTE_VELOCITY);

  capturedMidiMessages.clear();
  instrument->ProcessCommand(0, FourCC::InstrumentCommandPitchSlide, 0x007F);
  instrument->Render(0, buffer.data(), 4, true);
  CHECK(capturedMidiMessages.empty());

  std::destroy_at(instrument);
}

TEST_CASE("MIDI note zero receives a matching note off") {
  InstallFakeMidiService();
  alignas(MidiInstrument)
      std::array<std::byte, sizeof(MidiInstrument)> storage{};
  storage.fill(std::byte{0xFF});
  MidiInstrument *instrument =
      std::construct_at(reinterpret_cast<MidiInstrument *>(storage.data()));
  REQUIRE(instrument->Init());
  capturedMidiMessages.clear();

  REQUIRE(instrument->Start(0, 0));
  std::array<fixed, 8> buffer{};
  instrument->Render(0, buffer.data(), 4, false);
  instrument->Stop(0);

  REQUIRE(capturedMidiMessages.size() == 2U);
  CHECK(capturedMidiMessages[0].status_ == MidiMessage::MIDI_NOTE_ON);
  CHECK(capturedMidiMessages[0].data1_ == 0U);
  CHECK(capturedMidiMessages[1].status_ == MidiMessage::MIDI_NOTE_OFF);
  CHECK(capturedMidiMessages[1].data1_ == 0U);

  std::destroy_at(instrument);
}

TEST_CASE("Macro render starts its gain envelope from silence") {
  alignas(MacroInstrument)
      std::array<std::byte, sizeof(MacroInstrument)> layoutStorage{};
  MacroInstrument *layout = std::construct_at(
      reinterpret_cast<MacroInstrument *>(layoutStorage.data()));
  const std::size_t gainOffset = MacroInstrumentTestPeer::GainOffset(*layout);
  std::destroy_at(layout);
  REQUIRE(gainOffset + sizeof(std::uint16_t) <= sizeof(MacroInstrument));

  // Poison only the gain state so unrelated legacy Braids state stays valid.
  alignas(MacroInstrument)
      std::array<std::byte, sizeof(MacroInstrument)> storage{};
  storage[gainOffset] = std::byte{0xFF};
  storage[gainOffset + 1U] = std::byte{0xFF};
  MacroInstrument *instrument =
      std::construct_at(reinterpret_cast<MacroInstrument *>(storage.data()));
  REQUIRE(instrument->Init());
  // A slow attack also keeps Braids' legacy signed Mix arithmetic in range.
  instrument->FindVariable(FourCC::MacroInstrumentAttack)->SetInt(127);
  REQUIRE(instrument->Start(0, 60));
  std::array<fixed, 2> buffer{123, 123};

  REQUIRE(instrument->Render(0, buffer.data(), 1, false));
  CHECK(buffer[0] == 0);
  CHECK(buffer[1] == 0);

  std::destroy_at(instrument);
}

TEST_CASE("Sample size queries keep the default sentinel outside render state") {
  CHECK_FALSE(IsSampleRenderChannel(-1, SONG_CHANNEL_COUNT));
  CHECK(IsSampleRenderChannel(0, SONG_CHANNEL_COUNT));
  CHECK(IsSampleRenderChannel(SONG_CHANNEL_COUNT - 1, SONG_CHANNEL_COUNT));
  CHECK_FALSE(IsSampleRenderChannel(SONG_CHANNEL_COUNT, SONG_CHANNEL_COUNT));
}

TEST_CASE("UI2 Instrument descriptors preserve approved field layout") {
  using namespace ui2;
  CHECK(Ui2InstrumentFieldCount(IT_SAMPLE) == 17U);
  CHECK(Ui2InstrumentFieldCount(IT_MIDI) == 6U);
  CHECK(Ui2InstrumentFieldCount(IT_SID) == 11U);
  CHECK(Ui2InstrumentFieldCount(IT_OPAL) == 3U);
  CHECK(Ui2InstrumentOperatorCount(IT_OPAL) == 6U);

  const auto samplePan = Ui2InstrumentFieldParameter(IT_SAMPLE, 3U);
  CHECK(std::string_view(samplePan.label) == "PAN");
  CHECK(samplePan.minimum == 0);
  CHECK(samplePan.maximum == 0xFE);
  CHECK(samplePan.fineStep == 1U);
  CHECK(samplePan.coarseStep == 0x10U);

  const auto sidOscillator = Ui2InstrumentFieldParameter(IT_SID, 0U);
  CHECK(sidOscillator.primary == FourCC::SIDInstrumentOSCNumber);
  CHECK(sidOscillator.maximum == 2);
  const auto sidPulse = Ui2InstrumentFieldParameter(IT_SID, 1U);
  CHECK(sidPulse.maximum == 0xFFF);
  CHECK(Format(sidPulse, 0x800) == "800");
  const auto sid2Mode = Ui2InstrumentFieldParameter(IT_SID, 9U, false);
  CHECK(sid2Mode.primary == FourCC::SIDInstrument2FilterMode);
  const auto sid2Volume = Ui2InstrumentFieldParameter(IT_SID, 10U, false);
  CHECK(sid2Volume.primary == FourCC::SIDInstrument2Volume);

  const auto sampleStart = Ui2InstrumentFieldParameter(IT_SAMPLE, 12U);
  CHECK(Ui2InstrumentFieldParameter(IT_SAMPLE, 11U).primary ==
        FourCC::SampleInstrumentInterpolation);
  CHECK(sampleStart.primary == FourCC::SampleInstrumentStart);
  CHECK(sampleStart.width == 7U);
  CHECK(sampleStart.subfieldMode == Ui2InstrumentSubfieldMode::HexDigit);
  const auto resolved = Ui2ResolveSamplePositionMaximum(sampleStart, 1234);
  CHECK(resolved.maximum == 1233);
  const auto sampleLoopStart =
      Ui2InstrumentFieldParameter(IT_SAMPLE, 13U);
  CHECK(sampleLoopStart.primary == FourCC::SampleInstrumentLoopStart);
  CHECK(Ui2ResolveSamplePositionMaximum(sampleLoopStart, 1234).maximum ==
        1233);
  const auto sampleEnd = Ui2InstrumentFieldParameter(IT_SAMPLE, 14U);
  CHECK(sampleEnd.primary == FourCC::SampleInstrumentEnd);
  const auto resolvedEnd = Ui2ResolveSamplePositionMaximum(sampleEnd, 1234);
  CHECK(resolvedEnd.maximum == 1234);
  CHECK(Ui2AdjustInstrumentParameter(
            resolvedEnd, 1233, Ui2InstrumentValueDirection::Right) == 1234);
  CHECK(Ui2AdjustInstrumentParameter(
            resolvedEnd, 1234, Ui2InstrumentValueDirection::Right) == 1234);
  CHECK(Ui2InstrumentFieldParameter(IT_SAMPLE, 15U).primary ==
        FourCC::SampleInstrumentTable);
  CHECK(Ui2InstrumentFieldParameter(IT_SAMPLE, 16U).primary ==
        FourCC::SampleInstrumentTableAutomation);
  const auto midiTable = Ui2InstrumentFieldParameter(IT_MIDI, 5U);
  CHECK(midiTable.primary == FourCC::MidiInstrumentTable);
  CHECK(midiTable.maximum == TABLE_COUNT - 1);

  CHECK(Ui2InstrumentFieldParameter(IT_SID, 6U).primary ==
        FourCC::SIDInstrumentFilterOn);
}

TEST_CASE("UI2 Instrument TABLE activation allocates only Sample and MIDI "
          "table fields") {
  using namespace ui2;

  TableHolder tables;
  Variable sampleTable(FourCC::SampleInstrumentTable, VAR_OFF);
  const auto sampleDescriptor =
      Ui2InstrumentFieldParameter(IT_SAMPLE, 15U);
  REQUIRE(Ui2AllocateInstrumentTable(sampleDescriptor, sampleTable, tables));
  CHECK(sampleTable.GetInt() == 0);

  Variable midiTable(FourCC::MidiInstrumentTable, VAR_OFF);
  const auto midiDescriptor = Ui2InstrumentFieldParameter(IT_MIDI, 5U);
  REQUIRE(Ui2AllocateInstrumentTable(midiDescriptor, midiTable, tables));
  CHECK(midiTable.GetInt() == 1);

  Variable automation(FourCC::MidiInstrumentTableAutomation, 0);
  CHECK_FALSE(Ui2AllocateInstrumentTable(
      Ui2InstrumentFieldParameter(IT_MIDI, 4U), automation, tables));
  CHECK(automation.GetInt() == 0);

  Variable wrongVariable(FourCC::SampleInstrumentVolume, 0x7F);
  CHECK_FALSE(Ui2AllocateInstrumentTable(sampleDescriptor, wrongVariable,
                                         tables));
  CHECK(wrongVariable.GetInt() == 0x7F);

  for (int index = 0; index < TABLE_COUNT; ++index)
    tables.SetUsed(index);
  sampleTable.SetInt(7);
  CHECK_FALSE(
      Ui2AllocateInstrumentTable(sampleDescriptor, sampleTable, tables));
  CHECK(sampleTable.GetInt() == 7);
}

TEST_CASE("UI2 Instrument formatter covers decimal note boolean bitmask and OFF") {
  using namespace ui2;
  const auto channel = Ui2InstrumentFieldParameter(IT_MIDI, 0U);
  CHECK(Format(channel, 0) == "01");
  CHECK(Format(channel, 15) == "16");
  CHECK(Format(Ui2InstrumentFieldParameter(IT_SAMPLE, 4U), 60) == "C3");
  CHECK(Format(Ui2InstrumentFieldParameter(IT_MIDI, 4U), 0) == "NO");
  CHECK(Format(Ui2InstrumentFieldParameter(IT_MIDI, 4U), 1) == "YES");
  CHECK(Format(Ui2InstrumentFieldParameter(IT_MIDI, 3U), -1) == "--");
  CHECK(Format(Ui2InstrumentFieldParameter(IT_OPAL, 1U), 2) == "10");
  CHECK(Format(Ui2InstrumentOperatorParameter(3U, false), 0) == "SINE");
  CHECK(Format(Ui2InstrumentOperatorParameter(5U, false), 1) == "1.5");
  CHECK(Format(Ui2InstrumentFieldParameter(IT_SAMPLE, 9U), 0xDF, 0x1E) ==
        "LP / DF 1E");
}

TEST_CASE("UI2 Instrument adjustment uses each legacy fine coarse and wrap range") {
  using namespace ui2;
  const auto channel = Ui2InstrumentFieldParameter(IT_MIDI, 0U);
  CHECK(Ui2AdjustInstrumentParameter(channel, 0,
                                     Ui2InstrumentValueDirection::Up) == 4);
  CHECK(Ui2AdjustInstrumentParameter(channel, 15,
                                     Ui2InstrumentValueDirection::Right) == 15);

  const auto program = Ui2InstrumentFieldParameter(IT_MIDI, 3U);
  CHECK(Ui2InstrumentSideEffectFor(program, true, true) ==
        Ui2InstrumentEditSideEffect::SendMidiProgramChange);
  CHECK(Ui2InstrumentSideEffectFor(program, false, true) ==
        Ui2InstrumentEditSideEffect::None);
  FakeMidiInstrument midi;
  CHECK_FALSE(
      Ui2ApplyInstrumentSideEffect(program, false, true, midi, 3, 0x21));
  CHECK(midi.callCount == 0);
  CHECK(Ui2ApplyInstrumentSideEffect(program, true, true, midi, 3, 0x21));
  CHECK(midi.callCount == 1);
  CHECK(midi.lastChannel == 3);
  CHECK(midi.lastProgram == 0x21);
  CHECK(Ui2ApplyInstrumentSideEffect(program, true, true, midi, 3, -1));
  CHECK(midi.callCount == 2);
  CHECK(midi.lastProgram == -1);
  CHECK(Ui2AdjustInstrumentParameter(program, -1,
                                     Ui2InstrumentValueDirection::Left) == -1);
  CHECK(Ui2AdjustInstrumentParameter(program, -1,
                                     Ui2InstrumentValueDirection::Right) == 0);
  CHECK(Ui2AdjustInstrumentParameter(program, -1,
                                     Ui2InstrumentValueDirection::Up) == 16);
  CHECK(Ui2AdjustInstrumentParameter(program, 0,
                                     Ui2InstrumentValueDirection::Left) == -1);

  const auto adsr = Ui2InstrumentOperatorParameter(2U, false);
  CHECK(Ui2AdjustInstrumentParameter(adsr, 0xFFFF,
                                     Ui2InstrumentValueDirection::Up) == 0x0F);
  CHECK(Ui2AdjustInstrumentParameter(adsr, 0,
                                     Ui2InstrumentValueDirection::Down) ==
        0xFFF0);
}

TEST_CASE("UI2 Instrument big-hex and bit subfields preserve legacy semantics") {
  using namespace ui2;
  const auto adsr = Ui2InstrumentOperatorParameter(2U, false);
  const auto adsrSpec = Ui2InstrumentSubfields(adsr);
  CHECK(adsrSpec.mode == Ui2InstrumentSubfieldMode::HexDigit);
  CHECK(adsrSpec.count == 4U);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            adsr, 0x1234, adsrSpec.mode, 1U,
            Ui2InstrumentValueDirection::Up) == 0x1334);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            adsr, 0xFFFF, adsrSpec.mode, 3U,
            Ui2InstrumentValueDirection::Up) == 0x0000);

  const auto flags = Ui2InstrumentOperatorParameter(4U, true);
  const auto flagSpec = Ui2InstrumentSubfields(flags);
  CHECK(flagSpec.mode == Ui2InstrumentSubfieldMode::Bit);
  CHECK(flagSpec.count == 4U);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            flags, 0b0010, flagSpec.mode, 0U,
            Ui2InstrumentValueDirection::Down) == 0b1010);
  CHECK(Ui2AdjustInstrumentSubfieldParameter(
            flags, 0b0010, flagSpec.mode, 2U,
            Ui2InstrumentValueDirection::Up) == 0b0000);
}

TEST_CASE("UI2 Instrument adjustment legend applies only to approved numeric "
          "fields") {
  using namespace ui2;

  const Ui2InstrumentAdjustmentSpec volume =
      Ui2InstrumentAdjustment(Ui2InstrumentFieldParameter(IT_SAMPLE, 2U));
  CHECK(volume.visible);
  CHECK(volume.fineStep == 1U);
  CHECK(volume.coarseStep == 10U);
  CHECK_FALSE(volume.note);

  const Ui2InstrumentAdjustmentSpec root =
      Ui2InstrumentAdjustment(Ui2InstrumentFieldParameter(IT_SAMPLE, 4U));
  CHECK(root.visible);
  CHECK(root.note);
  CHECK(root.coarseStep == 12U);

  CHECK_FALSE(Ui2InstrumentAdjustment(
                  Ui2InstrumentFieldParameter(IT_SAMPLE, 0U))
                  .visible); // SAMPLE action
  CHECK_FALSE(Ui2InstrumentAdjustment(
                  Ui2InstrumentFieldParameter(IT_SAMPLE, 9U))
                  .visible); // combined FILTER
  CHECK_FALSE(Ui2InstrumentAdjustment(
                  Ui2InstrumentFieldParameter(IT_SAMPLE, 10U))
                  .visible); // LOOP choice
  CHECK_FALSE(Ui2InstrumentAdjustment(
                  Ui2InstrumentFieldParameter(IT_SAMPLE, 12U))
                  .visible); // hex digit
  CHECK_FALSE(Ui2InstrumentAdjustment(
                  Ui2InstrumentFieldParameter(IT_MIDI, 4U))
                  .visible); // boolean
  CHECK_FALSE(Ui2InstrumentAdjustment(
                  Ui2InstrumentFieldParameter(IT_OPAL, 1U))
                  .visible); // bit field
  CHECK_FALSE(
      Ui2InstrumentAdjustment(Ui2InstrumentOperatorParameter(2U, false))
          .visible); // operator hex digit
  CHECK(Ui2InstrumentAdjustment(
            Ui2InstrumentOperatorParameter(0U, false))
            .visible); // operator numeric
}

TEST_CASE("UI2 Instrument descriptors cannot write values outside field ranges") {
  using namespace ui2;
  constexpr std::array<InstrumentType, 4> types{IT_SAMPLE, IT_MIDI, IT_SID,
                                                IT_OPAL};
  constexpr std::array<Ui2InstrumentValueDirection, 4> directions{
      Ui2InstrumentValueDirection::Left, Ui2InstrumentValueDirection::Down,
      Ui2InstrumentValueDirection::Right, Ui2InstrumentValueDirection::Up};
  const auto check = [&](const Ui2InstrumentParameterDescriptor &descriptor) {
    if (!descriptor.editable)
      return;
    CAPTURE(descriptor.label);
    REQUIRE(descriptor.Valid());
    REQUIRE(descriptor.maximum >= descriptor.minimum);
    REQUIRE(descriptor.fineStep > 0U);
    REQUIRE(descriptor.coarseStep > 0U);
    for (const int current : {static_cast<int>(descriptor.minimum) - 100,
                              static_cast<int>(descriptor.minimum),
                              static_cast<int>(descriptor.maximum),
                              static_cast<int>(descriptor.maximum) + 100}) {
      for (const auto direction : directions) {
        const int adjusted =
            Ui2AdjustInstrumentParameter(descriptor, current, direction);
        CHECK(adjusted <= descriptor.maximum);
        const bool inRange = adjusted >= descriptor.minimum ||
                             (descriptor.offValue && adjusted == -1);
        CHECK(inRange);
      }
    }
  };

  for (const InstrumentType type : types) {
    for (std::uint8_t index = 0U; index < Ui2InstrumentFieldCount(type);
         ++index)
      check(Ui2InstrumentFieldParameter(type, index));
  }
  for (std::uint8_t index = 0U; index < Ui2InstrumentOperatorCount(IT_OPAL);
       ++index) {
    check(Ui2InstrumentOperatorParameter(index, false));
    check(Ui2InstrumentOperatorParameter(index, true));
  }
}

TEST_CASE("UI2 Instrument type change is blocked while playing and defaults NO") {
  using namespace ui2;
  Ui2InstrumentLifecycleController controller;
  CHECK_FALSE(controller.RequestTypeChange(IT_MIDI, IT_SAMPLE, true, true)
                  .HasValue());
  REQUIRE(controller.Active());
  CHECK(std::string_view(controller.Snapshot().title.data()) ==
        "Not while playing");
  CHECK_FALSE(Tap(controller, TrackerAction::Edit).HasValue());

  CHECK_FALSE(controller.RequestTypeChange(IT_MIDI, IT_SAMPLE, true, false)
                  .HasValue());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(std::string_view(dialog.title.data()) == "Change Instrument");
  CHECK(std::string_view(dialog.label.data()) == "Lose settings?");
  CHECK(dialog.actions[0] == UiDialogAction::Yes);
  CHECK(dialog.actions[1] == UiDialogAction::No);
  CHECK(dialog.selectedAction == 1U);
  CHECK_FALSE(Tap(controller, TrackerAction::Edit).HasValue());
}

TEST_CASE("UI2 Instrument type change emits only immediate-safe or explicit YES") {
  using namespace ui2;
  Ui2InstrumentLifecycleController controller;
  const auto immediate =
      controller.RequestTypeChange(IT_MIDI, IT_NONE, false, false);
  REQUIRE(immediate.type == Ui2InstrumentLifecycleCommandType::ApplyType);
  CHECK(immediate.instrumentType == IT_MIDI);

  CHECK_FALSE(controller.RequestTypeChange(IT_OPAL, IT_MIDI, true, false)
                  .HasValue());
  Tap(controller, TrackerAction::Left);
  const auto confirmed = Tap(controller, TrackerAction::Edit);
  REQUIRE(confirmed.type == Ui2InstrumentLifecycleCommandType::ApplyType);
  CHECK(confirmed.instrumentType == IT_OPAL);
}

TEST_CASE("UI2 Instrument type dialog ignores the held trigger until release") {
  using namespace ui2;
  Ui2InstrumentLifecycleController controller;

  CHECK_FALSE(controller
                  .RequestTypeChange(IT_OPAL, IT_MIDI, true, false,
                                     TrackerAction::Right)
                  .HasValue());
  REQUIRE(controller.Active());
  CHECK(controller.Snapshot().selectedAction == 1U); // NO

  // A platform repeat pulse from the RIGHT press that opened the dialog must
  // not move the conservative default to YES.
  CHECK_FALSE(controller.Handle(TrackerAction::Right, true).HasValue());
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
  CHECK_FALSE(controller.Handle(TrackerAction::Left, false).HasValue());
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK_FALSE(controller.Handle(TrackerAction::Right, false).HasValue());

  // Once released, a deliberate direction press still changes the choice.
  CHECK_FALSE(Tap(controller, TrackerAction::Left).HasValue());
  CHECK(controller.Snapshot().selectedAction == 0U); // YES
}

TEST_CASE("UI2 Instrument export overwrite requires explicit YES") {
  using namespace ui2;
  Ui2InstrumentLifecycleController controller;
  controller.RequestExportOverwrite();
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(std::string_view(dialog.title.data()) == "Overwrite existing file?");
  CHECK(dialog.actions[0] == UiDialogAction::Yes);
  CHECK(dialog.actions[1] == UiDialogAction::No);
  CHECK(dialog.selectedAction == 1U);
  CHECK_FALSE(Tap(controller, TrackerAction::Edit).HasValue());

  controller.RequestExportOverwrite(TrackerAction::Edit);
  CHECK_FALSE(controller.Handle(TrackerAction::Edit, true).HasValue());
  REQUIRE(controller.Active());
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK_FALSE(controller.Handle(TrackerAction::Left, true).HasValue());
  CHECK_FALSE(controller.Handle(TrackerAction::Left, false).HasValue());
  CHECK(controller.Snapshot().selectedAction == 1U);
  CHECK_FALSE(controller.Handle(TrackerAction::Edit, false).HasValue());
  Tap(controller, TrackerAction::Left);
  const auto overwrite = Tap(controller, TrackerAction::Edit);
  CHECK(overwrite.type == Ui2InstrumentLifecycleCommandType::OverwriteExport);
}

TEST_CASE("UI2 Instrument type confirmation follows modified variables only") {
  using namespace ui2;
  FakeVariable first;
  FakeVariable second;
  FakeInstrumentModificationState pristine{{&first, &second}, {}};
  CHECK_FALSE(Ui2InstrumentNeedsTypeChangeConfirmation(&pristine));

  second.modified = true;
  CHECK(Ui2InstrumentNeedsTypeChangeConfirmation(&pristine));
  second.modified = false;
  pristine.name = "lead";
  CHECK(Ui2InstrumentNeedsTypeChangeConfirmation(&pristine));
  CHECK_FALSE(Ui2InstrumentNeedsTypeChangeConfirmation<
              FakeInstrumentModificationState>(nullptr));
}

TEST_CASE("UI2 Instrument fixed-pool type transaction preserves failures") {
  using namespace ui2;
  FakeBank bank;
  bank.allocationExhausted_ = true;
  CHECK(Ui2ChangeInstrumentTypeAtomically(bank, 0U, FakeType::Midi) ==
        Ui2InstrumentTypeChangeResult::AllocationFailed);
  CHECK(bank.visible_ == &bank.original_);
  CHECK(bank.visible_->value == 73);

  bank.allocationExhausted_ = false;
  bank.commitAllowed_ = false;
  CHECK(Ui2ChangeInstrumentTypeAtomically(bank, 0U, FakeType::Midi) ==
        Ui2InstrumentTypeChangeResult::CommitFailed);
  CHECK(bank.visible_ == &bank.original_);
  CHECK_FALSE(bank.candidateInUse_);

  bank.commitAllowed_ = true;
  CHECK(Ui2ChangeInstrumentTypeAtomically(bank, 0U, FakeType::None) ==
        Ui2InstrumentTypeChangeResult::Changed);
  CHECK(bank.visible_ == &bank.candidate_);
  CHECK(bank.visible_->type == FakeType::None);
}

TEST_CASE("UI2 global settings transport accepts only plain PLAY") {
  using namespace ui2;
  constexpr std::uint16_t play = TrackerActionBit(TrackerAction::Play);
  CHECK(Ui2IsPlainPlay(TrackerAction::Play, true, play));
  CHECK_FALSE(Ui2IsPlainPlay(TrackerAction::Play, false, 0U));
  CHECK_FALSE(Ui2IsPlainPlay(
      TrackerAction::Play, true,
      play | TrackerActionBit(TrackerAction::Edit)));
  CHECK_FALSE(Ui2IsPlainPlay(TrackerAction::Right, true, play));
}
