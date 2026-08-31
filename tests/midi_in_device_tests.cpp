#include "doctest/doctest.h"

#include "Application/Player/Player.h"
#include "Services/Midi/MidiInDevice.h"

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
