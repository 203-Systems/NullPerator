#include "Application/Views/Ui2SampleSnapshot.h"
#include "Application/Views/Ui2RecordSnapshot.h"

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

TEST_CASE("UI2 Record snapshot projects fixed-capacity live state") {
  RecordViewUi2Snapshot snapshot;
  std::copy_n("ONBOARD MIC", 12U, snapshot.source.begin());
  std::copy_n("00:17", 6U, snapshot.elapsed.begin());
  snapshot.focus = RecordViewUi2Focus::Source;
  snapshot.state = RecordViewUi2State::Recording;
  snapshot.recordingAvailable = true;
  snapshot.meterAvailable = true;
  snapshot.meterSafeWidth = 120U;
  snapshot.meterWarningWidth = 40U;

  const ui2::UiRecordViewData data =
      snapshot.ViewData(ui2::UiPowerState::BatteryLow);
  CHECK(data.source == "ONBOARD MIC");
  CHECK(data.elapsed == "00:17");
  CHECK(data.focus == ui2::UiRecordFocus::Source);
  CHECK(data.state == ui2::UiRecordState::Recording);
  CHECK(data.safeWidth == 120U);
  CHECK(data.warningWidth == 40U);
  CHECK(data.power == ui2::UiPowerState::BatteryLow);

  snapshot.recordingAvailable = false;
  snapshot.meterAvailable = false;
  const ui2::UiRecordViewData unavailable = snapshot.ViewData();
  CHECK(unavailable.state == ui2::UiRecordState::Unavailable);
  CHECK(unavailable.focus == ui2::UiRecordFocus::None);
  CHECK_FALSE(unavailable.cursorInkVisible);
  CHECK_FALSE(unavailable.meterAvailable);
  CHECK(unavailable.safeWidth == 0U);
  CHECK(unavailable.warningWidth == 0U);
}

TEST_CASE("UI2 Record snapshot maps idle and saving without dynamic storage") {
  RecordViewUi2Snapshot snapshot;
  snapshot.recordingAvailable = true;
  snapshot.focus = RecordViewUi2Focus::Source;
  snapshot.state = RecordViewUi2State::Idle;
  CHECK(snapshot.ViewData().state == ui2::UiRecordState::Armed);

  snapshot.state = RecordViewUi2State::Saving;
  snapshot.savingPercent = 73U;
  const ui2::UiRecordViewData saving = snapshot.ViewData();
  CHECK(saving.state == ui2::UiRecordState::Saving);
  CHECK(saving.savingPercent == 73U);
}
