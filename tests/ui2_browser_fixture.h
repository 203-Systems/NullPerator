#pragma once

#include "UI2/Views/Browser/UiBrowserView.h"

namespace ui2::test {

inline UiBrowserViewData ApprovedBrowserFixture(std::string_view state) {
  UiBrowserViewData data;
  data.meta = "01";
  if (state == "sample-pool") {
    data.title = "SAMPLES";
    data.item = "AKWF 0906.WAV";
    data.footer = "13 KB  /  60";
    data.actions = {"EDIT", "IMPORT", "DELETE"};
    data.actionCount = 3;
    data.activeAction = 2;
  } else if (state == "instrument-import") {
    data.title = "IMPORT";
    data.item = "[..]";
    data.footer = "1 ITEM";
    data.actions = {"CANCEL", "OPEN", {}};
    data.actionCount = 2;
    data.activeAction = 1;
  } else if (state == "projects") {
    data.title = "BROWSE";
    data.meta = {};
    data.item = "ONECYCAC";
    data.footer = "1 ITEM";
    data.actions = {"LOAD", "DELETE", {}};
    data.actionCount = 2;
    data.activeAction = 1;
  } else {
    data.title = "THEMES";
    data.item = "DEFAULT.PT";
    data.footer = "1 ITEM";
    data.actions = {"CANCEL", "IMPORT", {}};
    data.actionCount = 2;
    data.activeAction = 1;
  }
  return data;
}

} // namespace ui2::test
