#pragma once

#include "UI2/Views/Phrase/UiPhraseView.h"

namespace ui2::test {

inline UiPhraseViewData ApprovedPhraseFixture(std::string_view state) {
  UiPhraseViewData data;
  data.number = "3A";
  data.rows = {{
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "END", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "KIL", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"C#4", "I00", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "???", "0000", "ARP", "0000"}},
      {{"C#4", "I00", "ARP", "0000", "ARP", "0000"}},
      {{"F5", "I01", "ARP", "0000", "ARP", "0000"}},
      {{"----", "I--", "ARP", "0000", "ARP", "0000"}},
      {{"F5", "I01", "ARP", "0004", "ARP", "0000"}},
      {{"F5", "I01", "ARP", "0000", "ARP", "0000"}},
  }};
  data.trackNotes = {"D3", "C4", "--", "F2", "A3", "D#3", "C3", "G2"};

  if (state == "note") {
    data.editRow = 9;
    data.editColumn = 0;
    data.activeHeader = UiPhraseHeader::Note;
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount = 2;
    data.cursorBottom.context.firstLine[0] = {
        .text = "INSTRUMENT 00", .color = UiColorToken::TextColored, .x = 9};
    data.cursorBottom.context.firstLine[1] = {
        .text = "AKWF 0906", .color = UiColorToken::TextNormal, .x = 94};
  } else if (state == "empty") {
    data.editRow = 1;
    data.editColumn = 0;
    data.activeHeader = UiPhraseHeader::None;
    data.cursorBottom.kind = UiBottomBarKind::Hidden;
  } else if (state == "fx") {
    data.editRow = 4;
    data.editColumn = 2;
    data.activeHeader = UiPhraseHeader::Fx1;
    data.cursorBottom.kind = UiBottomBarKind::Context;
    data.cursorBottom.context.firstLineCount = 2;
    data.cursorBottom.context.firstLine[0] = {
        .text = "KILL", .color = UiColorToken::TextColored, .x = 9};
    data.cursorBottom.context.firstLine[1] = {
        .text = "--BB", .color = UiColorToken::TextNormal, .x = 39};
    data.cursorBottom.context.secondLineCount = 1;
    data.cursorBottom.context.secondLine[0] = {
        .text = "STOP AUDIO AFTER BB TICKS",
        .color = UiColorToken::TextNormal,
        .x = 9};
  } else if (state == "number") {
    data.numberFocus = true;
    data.selectedTrack = 2;
  }
  return data;
}

} // namespace ui2::test
