#include "doctest/doctest.h"

#include "Application/UI2/Controllers/Ui2FeedbackController.h"

#include <cstring>
#include <type_traits>

TEST_CASE("UI2 feedback is fixed-capacity and expires without input") {
  ui2::Ui2FeedbackController feedback;
  CHECK_FALSE(feedback.Active());

  feedback.ShowError("NO FREE TABLE", 100U);
  REQUIRE(feedback.Active());
  const Ui2DialogSnapshot snapshot = feedback.Snapshot();
  CHECK(snapshot.kind == ui2::UiDialogKind::Feedback);
  CHECK(snapshot.tone == ui2::UiDialogTone::Error);
  CHECK(std::strcmp(snapshot.title.data(), "NO FREE TABLE") == 0);
  CHECK(snapshot.actionCount == 0U);

  CHECK_FALSE(feedback.Tick(100U +
                            ui2::Ui2FeedbackController::ErrorDurationMs - 1U));
  CHECK(feedback.Active());
  CHECK(feedback.Tick(100U +
                      ui2::Ui2FeedbackController::ErrorDurationMs));
  CHECK_FALSE(feedback.Active());

  static_assert(std::is_trivially_copyable_v<ui2::Ui2FeedbackController>);
}

TEST_CASE("UI2 feedback replacement refreshes identity and wrap-safe timing") {
  ui2::Ui2FeedbackController feedback;
  feedback.ShowMessage("READY", 0xFFFFFF00U);
  const std::uint32_t first = feedback.InstanceId();
  CHECK(feedback.Snapshot().tone == ui2::UiDialogTone::Message);
  CHECK_FALSE(feedback.Tick(0x00000020U));

  feedback.ShowError("FAILED", 0x00000020U);
  CHECK(feedback.InstanceId() == first + 1U);
  CHECK(std::strcmp(feedback.Snapshot().title.data(), "FAILED") == 0);

  feedback.ShowMessage("WRAP", 0xFFFFFF00U);
  CHECK_FALSE(feedback.Tick(0x00000607U));
  CHECK(feedback.Tick(0x00000608U));
  CHECK_FALSE(feedback.Active());
}

TEST_CASE("UI2 feedback truncates owned text and ignores empty publishes") {
  ui2::Ui2FeedbackController feedback;
  feedback.ShowError(
      "12345678901234567890123456789012345678901234567890", 10U);
  const Ui2DialogSnapshot snapshot = feedback.Snapshot();
  CHECK(snapshot.title.back() == '\0');
  CHECK(std::strlen(snapshot.title.data()) ==
        Ui2DialogSnapshot::TextCapacity - 1U);

  feedback.ShowMessage("", 20U);
  CHECK_FALSE(feedback.Active());
}
