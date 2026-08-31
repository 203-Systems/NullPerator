#include "doctest/doctest.h"

#include "Application/Player/Player.h"
#include "Services/Midi/MidiInDevice.h"

#include <vector>

namespace {

class TestMidiInDevice final : public MidiInDevice {
public:
  TestMidiInDevice() : MidiInDevice("test") {}

  bool Start() override { return MidiInDevice::Start(); }
  void Stop() override { MidiInDevice::Stop(); }
  void poll() override {}

  void Send(uint8_t status, uint8_t data1, uint8_t data2) {
    MidiMessage message(status, data1, data2);
    treatChannelEvent(message);
  }

  void Feed(uint8_t byte) { processMidiData(byte); }

  int driverStarts = 0;
  int driverStops = 0;
  bool startSucceeds = true;

protected:
  bool initDriver() override { return true; }
  void closeDriver() override {}
  bool startDriver() override {
    ++driverStarts;
    return startSucceeds;
  }
  void stopDriver() override { ++driverStops; }
};

} // namespace

namespace {

class MidiEventRecorder final : public I_Observer {
public:
  void Update(Observable &, I_ObservableData *data) override {
    if (data != nullptr) {
      messages.push_back(*static_cast<MidiMessage *>(data));
    } else {
      ++emptyUpdates;
    }
  }

  std::vector<MidiMessage> messages;
  int emptyUpdates = 0;
};

} // namespace

TEST_CASE("MIDI input honors channel-to-instrument assignments") {
  Player::ResetTestState();
  TestMidiInDevice device;
  MidiInDevice::AssignInstrumentToChannel(2, 7);

  device.Send(MidiMessage::MIDI_NOTE_ON + 2U, 60U, 100U);
  REQUIRE(Player::GetInstance()->playedNotes.size() == 1U);
  CHECK(Player::GetInstance()->playedNotes[0].instrument == 7U);
  CHECK(Player::GetInstance()->playedNotes[0].voice == 0U);

  device.Send(MidiMessage::MIDI_NOTE_OFF + 2U, 60U, 0U);
  REQUIRE(Player::GetInstance()->stoppedNotes.size() == 1U);
  CHECK(Player::GetInstance()->stoppedNotes[0].instrument == 7U);
  CHECK(Player::GetInstance()->stoppedNotes[0].voice == 0U);
}

TEST_CASE("Constructing another MIDI input preserves shared routing") {
  TestMidiInDevice first;
  MidiInDevice::AssignInstrumentToChannel(6, 9);

  TestMidiInDevice second;
  CHECK(MidiInDevice::GetInstrumentForChannel(6) == 9);
}

TEST_CASE("Unassigned MIDI input channels do not consume a live voice") {
  Player::ResetTestState();
  TestMidiInDevice device;
  MidiInDevice::ClearChannelAssignment(3);

  device.Send(MidiMessage::MIDI_NOTE_ON + 3U, 60U, 100U);
  CHECK(Player::GetInstance()->playedNotes.empty());

  MidiInDevice::AssignInstrumentToChannel(3, 5);
  device.Send(MidiMessage::MIDI_NOTE_ON + 3U, 61U, 100U);
  REQUIRE(Player::GetInstance()->playedNotes.size() == 1U);
  CHECK(Player::GetInstance()->playedNotes[0].instrument == 5U);
  CHECK(Player::GetInstance()->playedNotes[0].voice == 0U);
}

TEST_CASE("Stopping MIDI input releases every tracked live voice") {
  Player::ResetTestState();
  TestMidiInDevice device;
  MidiInDevice::AssignInstrumentToChannel(0, 4);
  MidiInDevice::AssignInstrumentToChannel(1, 5);
  REQUIRE(device.Start());

  device.Send(MidiMessage::MIDI_NOTE_ON, 60U, 100U);
  device.Send(MidiMessage::MIDI_NOTE_ON + 1U, 64U, 100U);
  REQUIRE(Player::GetInstance()->playedNotes.size() == 2U);

  device.Stop();
  REQUIRE(Player::GetInstance()->stoppedNotes.size() == 2U);
  CHECK(Player::GetInstance()->stoppedNotes[0].voice == 0U);
  CHECK(Player::GetInstance()->stoppedNotes[1].voice == 1U);
  CHECK(device.driverStops == 1);

  REQUIRE(device.Start());
  device.Send(MidiMessage::MIDI_NOTE_ON, 67U, 100U);
  REQUIRE(Player::GetInstance()->playedNotes.size() == 3U);
  CHECK(Player::GetInstance()->playedNotes.back().voice == 0U);
}

TEST_CASE("Restarting MIDI input cannot orphan a tracked voice") {
  Player::ResetTestState();
  TestMidiInDevice device;
  MidiInDevice::AssignInstrumentToChannel(0, 4);
  REQUIRE(device.Start());
  device.Send(MidiMessage::MIDI_NOTE_ON, 60U, 100U);

  REQUIRE(device.Start());
  REQUIRE(Player::GetInstance()->stoppedNotes.size() == 1U);
  CHECK(Player::GetInstance()->stoppedNotes[0].voice == 0U);
}

TEST_CASE("MIDI input stays stopped when its driver fails to start") {
  Player::ResetTestState();
  TestMidiInDevice device;
  device.startSucceeds = false;

  CHECK_FALSE(device.Start());
  CHECK_FALSE(device.IsRunning());
  CHECK(device.driverStarts == 1);
}

TEST_CASE("One-data-byte channel messages support running status") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0xC2U);
  device.Feed(10U);
  device.Feed(11U);
  device.Feed(12U);
  device.Feed(0xD3U);
  device.Feed(20U);
  device.Feed(21U);

  REQUIRE(recorder.messages.size() == 5U);
  CHECK(recorder.messages[0].status_ == 0xC2U);
  CHECK(recorder.messages[0].data1_ == 10U);
  CHECK(recorder.messages[1].status_ == 0xC2U);
  CHECK(recorder.messages[1].data1_ == 11U);
  CHECK(recorder.messages[2].status_ == 0xC2U);
  CHECK(recorder.messages[2].data1_ == 12U);
  CHECK(recorder.messages[3].status_ == 0xD3U);
  CHECK(recorder.messages[3].data1_ == 20U);
  CHECK(recorder.messages[4].status_ == 0xD3U);
  CHECK(recorder.messages[4].data1_ == 21U);
}

TEST_CASE("System Real-Time bytes preserve partial and running status") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0x92U);
  device.Feed(60U);
  device.Feed(0xF8U);
  device.Feed(100U);
  device.Feed(61U);
  device.Feed(0xFEU);
  device.Feed(101U);

  REQUIRE(recorder.messages.size() == 4U);
  CHECK(recorder.messages[0].status_ == 0xF8U);
  CHECK(recorder.messages[1].status_ == 0x92U);
  CHECK(recorder.messages[1].data1_ == 60U);
  CHECK(recorder.messages[1].data2_ == 100U);
  CHECK(recorder.messages[2].status_ == 0xFEU);
  CHECK(recorder.messages[3].status_ == 0x92U);
  CHECK(recorder.messages[3].data1_ == 61U);
  CHECK(recorder.messages[3].data2_ == 101U);
}

TEST_CASE("Transport input publishes only its typed raw message") {
  Player::ResetTestState();
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(MidiMessage::MIDI_START);

  REQUIRE(recorder.messages.size() == 1U);
  CHECK(recorder.messages[0].status_ == MidiMessage::MIDI_START);
  CHECK(recorder.emptyUpdates == 0);
  CHECK(Player::GetInstance()->songStartCalls == 1);
}

TEST_CASE("System Common messages cancel channel running status") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0x90U);
  device.Feed(60U);
  device.Feed(100U);
  device.Feed(0xF1U);
  device.Feed(7U);
  device.Feed(61U);
  device.Feed(101U);

  REQUIRE(recorder.messages.size() == 2U);
  CHECK(recorder.messages[0].status_ == 0x90U);
  CHECK(recorder.messages[1].status_ == 0xF1U);
  CHECK(recorder.messages[1].data1_ == 7U);

  device.Feed(0x90U);
  device.Feed(61U);
  device.Feed(101U);
  REQUIRE(recorder.messages.size() == 3U);
  CHECK(recorder.messages[2].status_ == 0x90U);
  CHECK(recorder.messages[2].data1_ == 61U);
  CHECK(recorder.messages[2].data2_ == 101U);
}

TEST_CASE("System Common data may be interrupted by System Real-Time") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0xF2U);
  device.Feed(1U);
  device.Feed(0xF8U);
  device.Feed(2U);

  REQUIRE(recorder.messages.size() == 2U);
  CHECK(recorder.messages[0].status_ == 0xF8U);
  CHECK(recorder.messages[1].status_ == 0xF2U);
  CHECK(recorder.messages[1].data1_ == 1U);
  CHECK(recorder.messages[1].data2_ == 2U);
}

TEST_CASE("Zero-data System Common messages cancel channel running status") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0x90U);
  device.Feed(60U);
  device.Feed(100U);
  device.Feed(0xF6U);
  device.Feed(61U);
  device.Feed(101U);

  REQUIRE(recorder.messages.size() == 2U);
  CHECK(recorder.messages[0].status_ == 0x90U);
  CHECK(recorder.messages[1].status_ == 0xF6U);
}

TEST_CASE("Ignored SysEx payload cannot leak into channel running status") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0x90U);
  device.Feed(60U);
  device.Feed(100U);
  device.Feed(0xF0U);
  device.Feed(1U);
  device.Feed(0xF8U);
  device.Feed(2U);
  device.Feed(0xF7U);
  device.Feed(61U);
  device.Feed(101U);

  REQUIRE(recorder.messages.size() == 3U);
  CHECK(recorder.messages[0].status_ == 0x90U);
  CHECK(recorder.messages[1].status_ == 0xF8U);
  CHECK(recorder.messages[2].status_ == 0xF7U);

  device.Feed(0x90U);
  device.Feed(61U);
  device.Feed(101U);
  REQUIRE(recorder.messages.size() == 4U);
  CHECK(recorder.messages[3].status_ == 0x90U);
}

TEST_CASE("A channel status terminates an ignored SysEx payload") {
  TestMidiInDevice device;
  MidiEventRecorder recorder;
  device.AddObserver(recorder);

  device.Feed(0xF0U);
  device.Feed(1U);
  device.Feed(0x91U);
  device.Feed(64U);
  device.Feed(110U);

  REQUIRE(recorder.messages.size() == 1U);
  CHECK(recorder.messages[0].status_ == 0x91U);
  CHECK(recorder.messages[0].data1_ == 64U);
  CHECK(recorder.messages[0].data2_ == 110U);
}
