#include "doctest/doctest.h"

#include "Services/Midi/MidiNoteTracker.h"

#include <cstdint>

TEST_CASE("MIDI note registration honors the requested audio channel") {
  MidiNoteTracker tracker;

  REQUIRE(tracker.registerNote(60U, 2U, 7U, 100U));
  CHECK(tracker.getAudioChannelForNote(60U, 2U) == 7);
  CHECK(tracker.getNextAvailableChannel() == 0);
  CHECK(tracker.unregisterNote(60U, 2U) == 7);
}

TEST_CASE("Repeated MIDI note-ons release one tracked voice per note-off") {
  MidiNoteTracker tracker;

  REQUIRE(tracker.registerNote(60U, 2U, 0U, 100U));
  REQUIRE(tracker.registerNote(60U, 2U, 1U, 90U));
  CHECK(tracker.isNoteActiveOnChannel(60U, 2U));

  CHECK(tracker.unregisterNote(60U, 2U) == 0);
  CHECK(tracker.isNoteActiveOnChannel(60U, 2U));
  CHECK(tracker.unregisterNote(60U, 2U) == 1);
  CHECK_FALSE(tracker.isNoteActiveOnChannel(60U, 2U));
  CHECK(tracker.unregisterNote(60U, 2U) == -1);
}

TEST_CASE("MIDI note tracking recovers after all eight voices are occupied") {
  MidiNoteTracker tracker;

  for (std::uint8_t channel = 0U; channel < MAX_NOTE_CHANNELS; ++channel) {
    REQUIRE(tracker.registerNote(static_cast<std::uint8_t>(60U + channel), 0U,
                                 channel, 100U));
  }
  CHECK(tracker.getNextAvailableChannel() == -1);

  CHECK(tracker.unregisterNote(63U, 0U) == 3);
  CHECK(tracker.getNextAvailableChannel() == 3);
  REQUIRE(tracker.registerNote(72U, 0U, 3U, 100U));
  CHECK(tracker.getAudioChannelForNote(72U, 0U) == 3);
  CHECK(tracker.getNextAvailableChannel() == -1);
}
