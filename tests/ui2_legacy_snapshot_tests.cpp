#include "Application/Views/Ui2SampleSnapshot.h"

#include "doctest/doctest.h"

#include <array>
#include <cstddef>
#include <cstdint>

TEST_CASE("UI2 legacy waveform capture is fixed-capacity and complete") {
  std::array<std::uint8_t, 320> source{};
  source.fill(80U);

  Ui2WaveformSnapshot snapshot;
  snapshot.Capture(source.data(), source.size(), 80U, 78U);

  CHECK(snapshot.size <= Ui2WaveformSnapshot::MaxEncodedBytes);
  CHECK(snapshot.size >= Ui2WaveformSnapshot::Width * 2U);
  CHECK(snapshot.revision != 0U);

  std::size_t cursor = 0;
  std::size_t columns = 0;
  while (columns < Ui2WaveformSnapshot::Width) {
    REQUIRE(cursor + 2U <= snapshot.size);
    const std::uint8_t start = snapshot.encoded[cursor++];
    const std::uint8_t run = snapshot.encoded[cursor++];
    if (start == 0xFFU && run == 0U) {
      ++columns;
      continue;
    }
    CHECK(start < 78U);
    CHECK(run > 0U);
    CHECK(static_cast<unsigned int>(start) + run <= 78U);
    cursor += (run + 3U) / 4U;
    REQUIRE(cursor <= snapshot.size);
    ++columns;
  }
  CHECK(cursor == snapshot.size);
}

TEST_CASE("UI2 legacy waveform capture preserves empty columns and revision") {
  std::array<std::uint8_t, 320> source{};
  for (std::size_t index = 120; index < 200; ++index)
    source[index] = static_cast<std::uint8_t>((index - 120U) % 40U);

  Ui2WaveformSnapshot first;
  first.Capture(source.data(), source.size(), 80U, 72U);
  Ui2WaveformSnapshot second;
  second.Capture(source.data(), source.size(), 80U, 72U);
  CHECK(first.size == second.size);
  CHECK(first.revision == second.revision);
  CHECK(first.Mask().size() == first.size);
  CHECK(first.encoded[0] == 0xFFU);
  CHECK(first.encoded[1] == 0U);

  source[160] = 80U;
  second.Capture(source.data(), source.size(), 80U, 72U);
  CHECK(second.revision != first.revision);
}

TEST_CASE("UI2 legacy snapshot helpers clamp geometry and text") {
  CHECK(Ui2WaveformX(0U, 0U, 1000U) == 0U);
  CHECK(Ui2WaveformX(500U, 0U, 1000U) == 110U);
  CHECK(Ui2WaveformX(1000U, 0U, 1000U) == 221U);
  CHECK(Ui2WaveformX(5U, 7U, 7U) == 0U);

  std::array<char, 5> text{};
  CopyUi2SnapshotText(text, "LONGER");
  CHECK(text[0] == 'L');
  CHECK(text[3] == 'G');
  CHECK(text[4] == '\0');

  Ui2WaveformMarkersSnapshot<2> markers;
  markers.Push(1U, Ui2WaveformMarkerKind::Start, true);
  markers.Push(2U, Ui2WaveformMarkerKind::End, false);
  markers.Push(3U, Ui2WaveformMarkerKind::Playhead, false);
  CHECK(markers.count == 2U);
  CHECK(markers.markers[0].selected);
  CHECK(markers.markers[1].kind == Ui2WaveformMarkerKind::End);
}
