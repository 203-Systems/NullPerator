#include "Services/Midi/MidiMessage.h"

#include "doctest/doctest.h"

#include <array>

TEST_CASE("MIDI wire encoding derives channel voice lengths from status") {
  const MidiWireMessage note = EncodeMidiWireMessage(
      MidiMessage(MidiMessage::MIDI_NOTE_ON | 2U, 0xFEU, 0xFFU));
  CHECK(note.length == 3U);
  CHECK((note.bytes == std::array<unsigned char, 3>{0x92U, 0x7EU, 0x7FU}));

  const MidiWireMessage program = EncodeMidiWireMessage(
      MidiMessage(MidiMessage::MIDI_PROGRAM_CHANGE | 4U, 0x87U,
                  MidiMessage::UNUSED_BYTE));
  CHECK(program.length == 2U);
  CHECK((program.bytes == std::array<unsigned char, 3>{0xC4U, 0x07U, 0U}));

  const MidiWireMessage pressure = EncodeMidiWireMessage(
      MidiMessage(MidiMessage::MIDI_CHANNEL_PRESSURE | 7U, 0x82U,
                  MidiMessage::UNUSED_BYTE));
  CHECK(pressure.length == 2U);
  CHECK((pressure.bytes == std::array<unsigned char, 3>{0xD7U, 0x02U, 0U}));
}

TEST_CASE("MIDI wire encoding handles fixed-length system messages") {
  const MidiWireMessage quarterFrame = EncodeMidiWireMessage(MidiMessage(
      MidiMessage::MIDI_TIME_CODE_QUARTER_FRAME, 0xFFU, 0xFFU));
  CHECK(quarterFrame.length == 2U);
  CHECK((quarterFrame.bytes ==
         std::array<unsigned char, 3>{0xF1U, 0x7FU, 0U}));

  const MidiWireMessage songPosition = EncodeMidiWireMessage(MidiMessage(
      MidiMessage::MIDI_SONG_POSITION_POINTER, 0x80U, 0xFFU));
  CHECK(songPosition.length == 3U);
  CHECK((songPosition.bytes ==
         std::array<unsigned char, 3>{0xF2U, 0U, 0x7FU}));

  const MidiWireMessage songSelect = EncodeMidiWireMessage(
      MidiMessage(MidiMessage::MIDI_SONG_SELECT, 0x81U, 0xFFU));
  CHECK(songSelect.length == 2U);
  CHECK((songSelect.bytes ==
         std::array<unsigned char, 3>{0xF3U, 0x01U, 0U}));

  const MidiWireMessage clock = EncodeMidiWireMessage(
      MidiMessage(MidiMessage::MIDI_CLOCK, 0xFFU, 0xFFU));
  CHECK(clock.length == 1U);
  CHECK((clock.bytes == std::array<unsigned char, 3>{0xF8U, 0U, 0U}));
}
