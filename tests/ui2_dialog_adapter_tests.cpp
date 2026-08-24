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

  source.assign(source.size(), 'X');
  const ui2::UiDialogViewData data = snapshot.ToViewData();
  CHECK(data.kind == ui2::UiDialogKind::TextInput);
  CHECK(data.title == "RENAME INSTRUMENT");
  CHECK(data.label == "NAME");
  CHECK(data.value == "AKWF_0906");
  CHECK(data.elapsed == "00:08");
  CHECK(data.progressWidth == 93U);
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
}
