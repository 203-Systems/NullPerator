#include "doctest/doctest.h"

#include "Application/UI2/Workflows/Ui2SampleEditorSaveWorkflow.h"

#include <array>
#include <cstdint>

TEST_CASE("UI2 sample save workflow retries only a preserved working "
          "generation") {
  using namespace ui2;

  CHECK(Ui2SampleEditorSaveWorkflow::ResolveFailure(
            Ui2SampleEditorTransactionResult::SaveFailedRetryable) ==
        Ui2SampleEditorSaveFailureResolution::KeepEditorForRetry);
  CHECK(Ui2SampleEditorSaveWorkflow::ResolveFailure(
            Ui2SampleEditorTransactionResult::SaveFailed) ==
        Ui2SampleEditorSaveFailureResolution::ReloadDestination);
  CHECK(Ui2SampleEditorSaveWorkflow::ResolveFailure(
            Ui2SampleEditorTransactionResult::RecoveryFailed) ==
        Ui2SampleEditorSaveFailureResolution::ReloadDestination);
}

TEST_CASE("UI2 sample SAVE and LOAD refreshes a Saved browser entry before a "
          "failed import") {
  using namespace ui2;
  enum class Event : std::uint8_t { Refresh, ImportFailed };
  std::array<Event, 2U> events{};
  std::uint8_t eventCount = 0U;

  const Ui2SampleEditorSaveFollowUp followUp =
      Ui2SampleEditorSaveWorkflow::PrepareFollowUp(
          Ui2SampleEditorTransactionResult::Saved, true, true,
          [&]() { events[eventCount++] = Event::Refresh; });
  if (followUp == Ui2SampleEditorSaveFollowUp::SaveAndLoad)
    events[eventCount++] = Event::ImportFailed;

  REQUIRE(eventCount == 2U);
  CHECK(events[0] == Event::Refresh);
  CHECK(events[1] == Event::ImportFailed);
}

TEST_CASE("UI2 sample save workflow does not refresh an unchanged directory "
          "entry") {
  using namespace ui2;
  bool refreshed = false;

  CHECK(Ui2SampleEditorSaveWorkflow::PrepareFollowUp(
            Ui2SampleEditorTransactionResult::NoChanges, false, true, [&]() {
              refreshed = true;
            }) == Ui2SampleEditorSaveFollowUp::ReturnToCaller);
  CHECK_FALSE(refreshed);
  CHECK(Ui2SampleEditorSaveWorkflow::PrepareFollowUp(
            Ui2SampleEditorTransactionResult::Saved, false, false, [&]() {
              refreshed = true;
            }) == Ui2SampleEditorSaveFollowUp::ReturnToCaller);
  CHECK_FALSE(refreshed);
}
