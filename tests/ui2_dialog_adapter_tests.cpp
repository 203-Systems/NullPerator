#include "Application/Views/ModalDialogs/Ui2DialogSnapshot.h"

#include "doctest/doctest.h"

#include <string>
#include <type_traits>

TEST_CASE("UI2 dialog snapshot owns every projected string") {
  std::string source = "RENAME INSTRUMENT";
  Ui2DialogSnapshot snapshot;
  snapshot.kind = ui2::UiDialogKind::TextInput;
  snapshot.SetTitle(source);
  snapshot.SetLabel("NAME");
  snapshot.SetValue("AKWF_0906");
  snapshot.SetElapsed("00:08");
  snapshot.SetProgressPercent(65);
  snapshot.PushAction(ui2::UiDialogAction::Yes);
  snapshot.PushAction(ui2::UiDialogAction::No);
  snapshot.SetSelectedAction(1, true);

  source.assign(source.size(), 'X');
  const ui2::UiDialogViewData data = snapshot.ToViewData();
  CHECK(data.kind == ui2::UiDialogKind::TextInput);
  CHECK(data.title == "RENAME INSTRUMENT");
  CHECK(data.label == "NAME");
  CHECK(data.value == "AKWF_0906");
  CHECK(data.elapsed == "00:08");
  CHECK(data.progressWidth == 93U);
  CHECK(data.actionCount == 2U);
  CHECK(data.actions[0] == ui2::UiDialogAction::Yes);
  CHECK(data.actions[1] == ui2::UiDialogAction::No);
  CHECK(data.selectedAction == 1U);
  CHECK(data.actionsFocused);
  CHECK(std::is_trivially_copyable_v<Ui2DialogSnapshot>);
}

TEST_CASE("UI2 dialog snapshot bounds text and progress") {
  Ui2DialogSnapshot snapshot;
  snapshot.SetTitle("1234567890123456789012345678901234567890");
  snapshot.SetProgressPercent(-1);
  CHECK(snapshot.ToViewData().title == "12345678901234567890123456789012");
  CHECK(snapshot.ToViewData().progressWidth == 0U);

  snapshot.SetProgressPercent(101);
  CHECK(snapshot.ToViewData().progressWidth ==
        Ui2DialogSnapshot::ProgressPixelWidth);

  Ui2DialogSnapshot copy = snapshot;
  copy.SetTitle("COPY");
  CHECK(snapshot.ToViewData().title != copy.ToViewData().title);
  CHECK(copy.ToViewData().title == "COPY");

  snapshot.PushAction(ui2::UiDialogAction::Ok);
  snapshot.PushAction(ui2::UiDialogAction::Yes);
  snapshot.PushAction(ui2::UiDialogAction::Cancel);
  snapshot.PushAction(ui2::UiDialogAction::No);
  snapshot.PushAction(ui2::UiDialogAction::Ok);
  snapshot.SetSelectedAction(9, false);
  CHECK(snapshot.ToViewData().actionCount == ui2::kUiDialogActionCapacity);
  CHECK(snapshot.ToViewData().selectedAction == 3U);
  CHECK_FALSE(snapshot.ToViewData().actionsFocused);
}
