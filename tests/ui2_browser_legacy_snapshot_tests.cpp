#include "Application/Views/Ui2BrowserSnapshot.h"

#include "doctest/doctest.h"

#include <string>

TEST_CASE("UI2 browser snapshot owns and bounds all renderer text") {
  Ui2BrowserSnapshot snapshot;
  std::string source(80U, 'X');
  Ui2BrowserSnapshot::CopyText(snapshot.title, "BROWSE");
  Ui2BrowserSnapshot::CopyText(snapshot.items[0], source);
  Ui2BrowserSnapshot::CopyText(snapshot.actions[0], "LOAD");
  snapshot.ConfigureWindow(1U, 0U, 0U);
  snapshot.actionCount = 1U;

  source.assign(80U, 'Y');
  const ui2::UiBrowserViewData data = snapshot.ViewData();
  CHECK(data.title == "BROWSE");
  CHECK(data.items[0].size() == Ui2BrowserSnapshot::ItemTextCapacity - 1U);
  CHECK(data.items[0].front() == 'X');
  CHECK(data.items[0].back() == 'X');
  CHECK(data.actions[0] == "LOAD");
  CHECK(data.visibleItemCount == 1U);
  CHECK(data.cursorInkVisible);
}

TEST_CASE("UI2 browser snapshot reconciles legacy and 13-row windows") {
  Ui2BrowserSnapshot snapshot;

  SUBCASE("preserves a legacy top when selection fits") {
    snapshot.ConfigureWindow(40U, 14U, 7U);
    CHECK(snapshot.topIndex == 7U);
    CHECK(snapshot.selectedRow == 7U);
    CHECK(snapshot.visibleItemCount == 13U);
  }

  SUBCASE("brings a lower legacy selection into the final visible row") {
    snapshot.ConfigureWindow(40U, 20U, 0U);
    CHECK(snapshot.topIndex == 8U);
    CHECK(snapshot.selectedRow == 12U);
    CHECK(snapshot.visibleItemCount == 13U);
  }

  SUBCASE("clamps the final short window") {
    snapshot.ConfigureWindow(20U, 99U, 99U);
    CHECK(snapshot.topIndex == 7U);
    CHECK(snapshot.selectedRow == 12U);
    CHECK(snapshot.visibleItemCount == 13U);
  }

  SUBCASE("empty lists never expose a cursor") {
    snapshot.ConfigureWindow(0U, 9U, 9U);
    const ui2::UiBrowserViewData data = snapshot.ViewData();
    CHECK(snapshot.topIndex == 0U);
    CHECK(snapshot.visibleItemCount == 0U);
    CHECK_FALSE(snapshot.hasSelection);
    CHECK_FALSE(data.cursorInkVisible);
  }
}

TEST_CASE("UI2 browser snapshot clamps malformed action metadata") {
  Ui2BrowserSnapshot snapshot;
  snapshot.actionCount = 255U;
  snapshot.activeAction = 255U;
  const ui2::UiBrowserViewData data = snapshot.ViewData();
  CHECK(data.actionCount == Ui2BrowserSnapshot::ActionCapacity);
  CHECK(data.activeAction == Ui2BrowserSnapshot::ActionCapacity - 1U);
}
