#pragma once

#include "UI2/Views/Table/UiTableView.h"

namespace ui2::test {

inline UiTableViewData ApprovedTableFixture(std::string_view state) {
  UiTableViewData data;
  data.number = state.starts_with("instrument") ? "I00" : "P00";
  for (auto &row : data.rows) {
    row = {"---", "0000", "---", "0000", "---", "0000"};
  }
  data.trackNotes = {"D3", "C4", "--", "F2", "A3", "D#3", "C3", "G2"};
  data.editRow = 0;
  data.editColumn = 0;
  data.activeHeader = UiTableHeader::Fx1;
  data.selectedTrack = 2;

  if (state.ends_with("number")) {
    data.numberFocus = true;
    return data;
  }
  data.cursorBottom.kind = UiBottomBarKind::Context;
  data.cursorBottom.context.firstLineCount = 2;
  data.cursorBottom.context.secondLineCount = 1;
  if (state.starts_with("instrument")) {
    data.cursorBottom.context.firstLine[0] = {
        .text = "FILTER", .color = UiColorToken::CursorPrimary, .x = 9};
    data.cursorBottom.context.firstLine[1] = {
        .text = "AABB", .color = UiColorToken::TextPrimary, .x = 51};
    data.cursorBottom.context.secondLine[0] = {
        .text = "CUTOFF AA / RESONANCE BB",
        .color = UiColorToken::TextPrimary,
        .x = 9};
  } else {
    data.cursorBottom.context.firstLine[0] = {
        .text = "KILL", .color = UiColorToken::CursorPrimary, .x = 9};
    data.cursorBottom.context.firstLine[1] = {
        .text = "--BB", .color = UiColorToken::TextPrimary, .x = 39};
    data.cursorBottom.context.secondLine[0] = {
        .text = "STOP AUDIO AFTER BB TICKS",
        .color = UiColorToken::TextPrimary,
        .x = 9};
  }
  return data;
}

} // namespace ui2::test
