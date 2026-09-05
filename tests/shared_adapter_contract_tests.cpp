#include "Adapters/posix/filesystem/PosixFile.h"
#include "Application/UI2/Workflows/Ui2SampleWorkflow.h"
#include "doctest/doctest.h"
#include <cstdio>
#include <unistd.h>

TEST_CASE("Native sync errors are distinct from browser buffered completion") {
  int descriptors[2]{};
  REQUIRE(::pipe(descriptors) == 0);
  {
    PosixFile native(::fdopen(descriptors[1], "w"));
    REQUIRE(native.Write("x", 1, 1) == 1);
    CHECK_FALSE(native.Sync()); // A pipe can flush, but cannot fsync.
  }
  ::close(descriptors[0]);
  REQUIRE(::pipe(descriptors) == 0);
  {
    PosixFile browser(::fdopen(descriptors[1], "w"), false,
                      {StoragePolicy::SyncMode::Buffered, nullptr});
    REQUIRE(browser.Write("x", 1, 1) == 1);
    CHECK(browser.Sync());
  }
  ::close(descriptors[0]);
}

namespace {
int stops = 0;
std::uint8_t stoppedInstrument = 0;
ui2::Ui2SampleWorkflow::PreviewKind stoppedKind{};
void StopPreview(ui2::Ui2SampleWorkflow::PreviewKind kind,
                 std::uint8_t instrument) {
  ++stops;
  stoppedKind = kind;
  stoppedInstrument = instrument;
}
} // namespace
TEST_CASE("Sample workflow boundaries retire preview ownership once") {
  using Workflow = ui2::Ui2SampleWorkflow;
  Workflow samples(&StopPreview);
  stops = 0;
  for (auto kind : {Workflow::PreviewKind::EditorStream,
                    Workflow::PreviewKind::SliceNote}) {
    samples.preview.kind = kind;
    samples.preview.instrument = 7;
    samples.preview.rate = 44100;
    samples.returnPage = ui2::UiApplicationPage::Browser;
    const int before = stops;
    samples.Reset();
    CHECK(stops == before + 1);
    CHECK(stoppedKind == kind);
    CHECK(stoppedInstrument == 7);
    CHECK(samples.preview.kind == Workflow::PreviewKind::None);
    CHECK_FALSE(samples.editor.Active());
    CHECK_FALSE(samples.slices.Active());
    CHECK_FALSE(samples.transaction.Active());
    CHECK_FALSE(samples.waveform.Ready());
    CHECK(samples.returnPage == ui2::UiApplicationPage::Instrument);
    samples.Reset();
    CHECK(stops == before + 1);
  }
}
