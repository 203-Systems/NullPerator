#include "Application/Views/Ui2BrowserSnapshot.h"
#include "Application/Views/Ui2BrowserItemFormat.h"
#include "UI2/Text/UiFont5x7.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <array>
#include <cstdint>
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

TEST_CASE("UI2 browser marker glyphs have explicit 5x7 ink") {
  constexpr std::array markers{'*', '~', '[', ']'};
  for (const char marker : markers) {
    const ui2::UiFont5x7::Rows rows = ui2::UiFont5x7::Glyph(marker);
    CHECK(std::any_of(rows.begin(), rows.end(),
                      [](std::uint8_t row) { return row != 0U; }));
  }
  CHECK(ui2::UiFont5x7::Glyph('[') != ui2::UiFont5x7::Glyph(']'));
}

TEST_CASE("UI2 Instrument Import renders the parent entry as literal dots") {
  std::array<char, 16> display{};
  ui2::FormatInstrumentImportBrowserItem(display.data(), display.size(), "..",
                                         true);
  CHECK(std::string_view(display.data()) == "..");

  ui2::FormatInstrumentImportBrowserItem(display.data(), display.size(),
                                         "SYNTHS", true);
  CHECK(std::string_view(display.data()) == "[SYNTHS]");
}
