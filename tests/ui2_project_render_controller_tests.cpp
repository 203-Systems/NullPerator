/* SPDX-License-Identifier: BSD-3-Clause */

#include <doctest/doctest.h>

#include "Application/UI2/Controllers/Ui2ProjectRenderController.h"

#include <array>
#include <string_view>
#include <type_traits>

namespace {

class FakeRenderBackend final : public ui2::IUi2ProjectRenderBackend {
public:
  ui2::Ui2ProjectRenderStartResult startResult =
      ui2::Ui2ProjectRenderStartResult::Started;
  ui2::Ui2ProjectRenderPlaybackSnapshot playback{};
  std::array<std::array<std::uint8_t, SONG_CHANNEL_COUNT>, SONG_ROW_COUNT>
      phraseCounts{};
  ui2::Ui2ProjectRenderMode startedMode = ui2::Ui2ProjectRenderMode::Mixdown;
  int startCalls = 0;
  int stopCalls = 0;
  bool running = false;
  bool failed = false;

  ui2::Ui2ProjectRenderStartResult
  Start(ui2::Ui2ProjectRenderMode mode) override {
    ++startCalls;
    startedMode = mode;
    running = startResult == ui2::Ui2ProjectRenderStartResult::Started;
    return startResult;
  }
  void Stop() override {
    ++stopCalls;
    running = false;
  }
  bool IsRunning() const override { return running; }
  bool Failed() const override { return failed; }
  ui2::Ui2ProjectRenderPlaybackSnapshot CapturePlayback() const override {
    return playback;
  }
  int ChainPhraseCount(int row, int channel) const override {
    if (row < 0 || row >= SONG_ROW_COUNT || channel < 0 ||
        channel >= SONG_CHANNEL_COUNT)
      return 0;
    return phraseCounts[row][channel];
  }
};

void Tap(ui2::Ui2ProjectRenderController &controller, TrackerAction action) {
  controller.Handle(action, true);
  controller.Handle(action, false);
}

std::string_view Text(const auto &value) { return value.data(); }

} // namespace

TEST_CASE("UI2 Project render preserves legacy empty-row and busy guards") {
  using namespace ui2;
  FakeRenderBackend backend;
  Ui2ProjectRenderController controller(backend);

  backend.startResult = Ui2ProjectRenderStartResult::PlayerBusy;
  CHECK_FALSE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  CHECK(controller.LastStartResult() ==
        Ui2ProjectRenderStartResult::PlayerBusy);
  CHECK_FALSE(controller.Active());

  backend.startResult = Ui2ProjectRenderStartResult::EmptyFirstSongRow;
  CHECK_FALSE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot empty = controller.Snapshot();
  CHECK(empty.kind == UiDialogKind::Message);
  CHECK(Text(empty.title) == "Render failed");
  CHECK(Text(empty.label) == "Song row 00 has no phrases");
  REQUIRE(empty.actionCount == 1U);
  CHECK(empty.actions[0] == UiDialogAction::Ok);
  Tap(controller, TrackerAction::Enter);
  CHECK_FALSE(controller.Active());
}

TEST_CASE("UI2 Project render starts the selected legacy output mode") {
  using namespace ui2;
  FakeRenderBackend backend;
  backend.playback.active[0] = true;
  backend.phraseCounts[0][0] = 1U;
  Ui2ProjectRenderController controller(backend);

  REQUIRE(controller.Request(Ui2ProjectRenderMode::Stems));
  CHECK(backend.startedMode == Ui2ProjectRenderMode::Stems);
  const Ui2DialogSnapshot dialog = controller.Snapshot();
  CHECK(dialog.kind == UiDialogKind::RenderProgress);
  CHECK(Text(dialog.title) == "Stems Rendering");
  CHECK(Text(dialog.label).empty());
  CHECK(Text(dialog.elapsed) == "0%");
  REQUIRE(dialog.actionCount == 1U);
  CHECK(dialog.actions[0] == UiDialogAction::Cancel);
}

TEST_CASE("UI2 Project render percent matches legacy shortest active channel") {
  using namespace ui2;
  FakeRenderBackend backend;
  backend.playback.active[0] = true;
  backend.playback.active[1] = true;
  backend.phraseCounts[0][0] = 2U;
  backend.phraseCounts[1][0] = 2U;
  backend.phraseCounts[0][1] = 1U;
  backend.phraseCounts[1][1] = 1U;
  Ui2ProjectRenderController controller(backend);

  REQUIRE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  CHECK(controller.ProgressPercent() == 0);

  backend.playback.phraseRow[1] = 8;
  controller.Tick();
  CHECK(controller.ProgressPercent() == 25);

  backend.playback.songRow[1] = 1;
  backend.playback.phraseRow[1] = 0;
  controller.Tick();
  CHECK(controller.ProgressPercent() == 50);

  // UI progress never travels backwards when async channel snapshots briefly
  // regress during a callback boundary.
  backend.playback.songRow[1] = 0;
  backend.playback.phraseRow[1] = 1;
  controller.Tick();
  CHECK(controller.ProgressPercent() == 50);
}

TEST_CASE("UI2 Project render naturally completes at 100 percent") {
  using namespace ui2;
  FakeRenderBackend backend;
  backend.playback.active[0] = true;
  backend.phraseCounts[0][0] = 1U;
  Ui2ProjectRenderController controller(backend);

  REQUIRE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  backend.running = false;
  controller.Tick();
  REQUIRE(controller.Active());
  CHECK_FALSE(controller.Rendering());
  CHECK(backend.stopCalls == 0);
  const Ui2DialogSnapshot complete = controller.Snapshot();
  CHECK(Text(complete.label) == "Render Complete!");
  CHECK(Text(complete.elapsed) == "100%");
  CHECK(complete.progressWidth == Ui2DialogSnapshot::ProgressPixelWidth);
  REQUIRE(complete.actionCount == 1U);
  CHECK(complete.actions[0] == UiDialogAction::Ok);
  Tap(controller, TrackerAction::Enter);
  CHECK_FALSE(controller.Active());
}

TEST_CASE("UI2 Project render cancel stops Player then uses existing message") {
  using namespace ui2;
  FakeRenderBackend backend;
  backend.playback.active[0] = true;
  backend.phraseCounts[0][0] = 1U;
  Ui2ProjectRenderController controller(backend);

  REQUIRE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  Tap(controller, TrackerAction::Enter);
  CHECK(backend.stopCalls == 1);
  CHECK_FALSE(backend.running);
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot stopped = controller.Snapshot();
  CHECK(stopped.kind == UiDialogKind::Message);
  CHECK(Text(stopped.title) == "Rendering Stopped");
  CHECK(Text(stopped.label).empty());
  Tap(controller, TrackerAction::Enter);
  CHECK_FALSE(controller.Active());
}

TEST_CASE("UI2 Project render exposes writer-open failure safely") {
  using namespace ui2;
  FakeRenderBackend backend;
  backend.startResult = Ui2ProjectRenderStartResult::OutputUnavailable;
  Ui2ProjectRenderController controller(backend);

  CHECK_FALSE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  REQUIRE(controller.Active());
  const Ui2DialogSnapshot failed = controller.Snapshot();
  CHECK(failed.kind == UiDialogKind::Message);
  CHECK(Text(failed.title) == "Render failed");
  CHECK(Text(failed.label) == "Could not open file");
}

TEST_CASE("UI2 Project render reset safely unwinds an active Player") {
  using namespace ui2;
  FakeRenderBackend backend;
  backend.playback.active[0] = true;
  backend.phraseCounts[0][0] = 1U;
  Ui2ProjectRenderController controller(backend);

  REQUIRE(controller.Request(Ui2ProjectRenderMode::Mixdown));
  controller.Reset();
  CHECK(backend.stopCalls == 1);
  CHECK_FALSE(backend.running);
  CHECK_FALSE(controller.Active());
}

static_assert(
    std::is_trivially_copyable_v<ui2::Ui2ProjectRenderPlaybackSnapshot>);
static_assert(sizeof(ui2::Ui2ProjectRenderController) <= 64U);

TEST_CASE("UI2 render never reports completion after output failure") {
  for (bool stillPlaying : {false, true}) {
    FakeRenderBackend backend;
    ui2::Ui2ProjectRenderController controller(backend);
    REQUIRE(controller.Request(ui2::Ui2ProjectRenderMode::Stems));
    backend.running = stillPlaying;
    backend.failed = true;
    controller.Tick();
    CHECK_FALSE(controller.Rendering());
    CHECK(Text(controller.Snapshot().title) == "Render failed");
    CHECK(Text(controller.Snapshot().label) == "Could not write file");
    CHECK(backend.stopCalls == 1);
  }
}
