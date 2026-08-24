#pragma once

#include "UI2/Views/Project/UiProjectView.h"

namespace ui2::test {

inline UiProjectViewData ApprovedProjectFixture(std::string_view state) {
  UiProjectViewData data;
  if (state == "name")
    data.cursor = UiProjectCursor::Name;
  else if (state == "cleanup")
    data.cursor = UiProjectCursor::Samples;
  else if (state == "render")
    data.cursor = UiProjectCursor::Render;
  else
    data.cursor = UiProjectCursor::Tempo;
  return data;
}

} // namespace ui2::test
