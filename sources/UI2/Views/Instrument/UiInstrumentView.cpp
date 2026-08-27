/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "UI2/Views/Instrument/UiInstrumentView.h"

#include "UI2/Render/UiFrameRenderer.h"
#include "UI2/Text/UiFont5x7.h"

#include <algorithm>
#include <array>

namespace ui2 {
namespace {

constexpr std::array<std::string_view, 5> kTypeOptions{"NONE", "SAMPLE", "MIDI",
                                                       "SID", "OPAL"};
constexpr std::array<std::string_view, 2> kBooleanOptions{"OFF", "ON"};
constexpr std::array<std::string_view, 2> kInterpolationOptions{"LINEAR", "NONE"};
constexpr std::array<std::string_view, 5> kLoopOptions{
    "NONE", "LOOP", "PINGPONG", "OSCILLATOR", "LOOPSYNC"};

std::string_view TypeName(UiInstrumentKind kind) {
  return kTypeOptions[static_cast<std::size_t>(kind)];
}

RectI16 ResolvedCursorRect(const UiInstrumentViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    return Intersect(data.cursorVisualRect, RectI16::Screen());
  }
  return UiInstrumentView::CursorTargetRect(data);
}

RectI16 ExpandedCursorDamage(RectI16 rect) {
  if (rect.Empty())
    return {};
  return Intersect({static_cast<std::int16_t>(rect.x - 1),
                    static_cast<std::int16_t>(rect.y - 1),
                    static_cast<std::int16_t>(rect.width + 2),
                    static_cast<std::int16_t>(rect.height + 2)},
                   RectI16::Screen());
}

void DrawField(UiSceneBuilder<256, 1024> &builder, std::string_view label,
               std::string_view value, std::int16_t y,
               UiColorToken valueColor, bool userData = false) {
  builder.Text(label, 9, y, UiColorToken::TextDim);
  if (userData)
    builder.UserText(value, 92, y, valueColor);
  else
    builder.Text(value, 92, y, valueColor);
}

void DrawSection(UiSceneBuilder<256, 1024> &builder, std::string_view label,
                 std::int16_t y) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.Text(label, 9, y, UiColorToken::TextColored);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(222 - width), 1},
               UiColorToken::CursorRow);
}

struct SelectedValueLayout {
  std::string_view text{};
  std::int16_t x = 0;
  std::int16_t y = 0;
  bool userData = false;
};

SelectedValueLayout SelectedValue(const UiInstrumentViewData &data) {
  if (data.cursor == UiInstrumentCursor::Field &&
      data.selectedField < data.fieldCount) {
    const UiInstrumentField &field = data.fields[data.selectedField];
    return {.text = field.value,
            .x = 92,
            .y = field.y,
            .userData = field.userData};
  }
  if ((data.cursor == UiInstrumentCursor::Operator1 ||
       data.cursor == UiInstrumentCursor::Operator2) &&
      data.selectedOperator < data.operatorCount) {
    const UiInstrumentOperatorRow &row =
        data.operators[data.selectedOperator];
    return {.text = data.cursor == UiInstrumentCursor::Operator1 ? row.op1
                                                                  : row.op2,
            .x = static_cast<std::int16_t>(
                data.cursor == UiInstrumentCursor::Operator1 ? 144 : 190),
            .y = static_cast<std::int16_t>(144 +
                                           data.selectedOperator * 9)};
  }
  return {};
}

bool SelectedSubfield(const UiInstrumentViewData &data,
                      SelectedValueLayout &layout,
                      std::uint8_t &textIndex) {
  if (!data.enterSubfieldFocus)
    return false;
  layout = SelectedValue(data);
  const std::uint16_t index = static_cast<std::uint16_t>(
      data.subfieldTextOffset + data.selectedSubfield);
  if (layout.text.empty() || index >= layout.text.size())
    return false;
  textIndex = static_cast<std::uint8_t>(index);
  return true;
}

bool BottomVisible(const UiInstrumentViewData &data) {
  return data.numberFocus || data.adjustmentFocus ||
         data.cursor == UiInstrumentCursor::Name ||
         data.cursor == UiInstrumentCursor::Type ||
         data.fieldBottom != UiInstrumentFieldBottom::Hidden;
}

} // namespace

RectI16 UiInstrumentView::CursorTargetRect(const UiInstrumentViewData &data) {
  SelectedValueLayout layout;
  std::uint8_t textIndex = 0U;
  if (SelectedSubfield(data, layout, textIndex)) {
    return {static_cast<std::int16_t>(
                layout.x + textIndex * UiFont5x7::kAdvance - 2),
            static_cast<std::int16_t>(layout.y - 1),
            static_cast<std::int16_t>(UiFont5x7::kGlyphWidth + 4), 9};
  }
  switch (data.cursor) {
  case UiInstrumentCursor::Name:
    return {7, 41, 226, 9};
  case UiInstrumentCursor::Type:
    return {7, 53, 226, 9};
  case UiInstrumentCursor::Field:
    if (data.selectedField < data.fieldCount) {
      return {7,
              static_cast<std::int16_t>(data.fields[data.selectedField].y - 1),
              226, 9};
    }
    return {};
  case UiInstrumentCursor::Operator1:
  case UiInstrumentCursor::Operator2:
    if (data.selectedOperator < data.operatorCount) {
      const std::int16_t y =
          static_cast<std::int16_t>(143 + data.selectedOperator * 9);
      return {static_cast<std::int16_t>(
                  data.cursor == UiInstrumentCursor::Operator1 ? 139 : 185),
              y, 40, 9};
    }
    return {};
  case UiInstrumentCursor::None:
    return {};
  }
  return {};
}

std::int16_t UiInstrumentView::ContentBottom(
    const UiInstrumentViewData &data) {
  std::int16_t bottom = 63;
  for (std::uint8_t index = 0; index < data.fieldCount; ++index) {
    bottom = std::max(bottom,
                      static_cast<std::int16_t>(data.fields[index].y + 8));
  }
  for (std::uint8_t index = 0; index < data.operatorCount; ++index) {
    bottom = std::max(bottom,
                      static_cast<std::int16_t>(151 + index * 9));
  }
  return bottom;
}

std::int16_t UiInstrumentView::RevealCursor(
    std::int16_t currentOffset, const UiInstrumentViewData &data) {
  const std::int16_t viewportBottom = BottomVisible(data) ? 208 : 240;
  return UiVerticalList::Reveal(currentOffset, CursorTargetRect(data), 34,
                                viewportBottom, ContentBottom(data));
}

RectI16 UiInstrumentView::FieldDamageRect(std::int16_t y) {
  return Intersect({5, static_cast<std::int16_t>(y - 1), 230, 11},
                   RectI16::Screen());
}

bool UiInstrumentView::RequiresFullInvalidation(
    const UiInstrumentViewData &previous, const UiInstrumentViewData &current) {
  return previous.kind != current.kind ||
         previous.numberFocus != current.numberFocus ||
         BottomVisible(previous) != BottomVisible(current) ||
         previous.fieldCount != current.fieldCount ||
         previous.operatorCount != current.operatorCount;
}

void UiInstrumentView::RenderDelta(const UiInstrumentViewData &previous,
                                   const UiInstrumentViewData &current,
                                   const UiFrameScene &currentScene,
                                   UiIndexedSurface &surface,
                                   const UiPalette &palette) {
  if (RequiresFullInvalidation(previous, current)) {
    UiFrameRenderer::RenderStatic(currentScene, surface, palette);
    return;
  }
  const auto render = [&](RectI16 rect) {
    UiFrameRenderer::RenderRegion(currentScene, surface, palette, rect);
  };
  const auto contentRect = [&](RectI16 rect) {
    return UiVerticalList::VisualRect(rect, currentScene.contentOffsetY);
  };
  if (previous.number != current.number ||
      previous.topMetaVisualRect != current.topMetaVisualRect ||
      previous.topMetaVisualOverride != current.topMetaVisualOverride ||
      previous.topMetaInkVisible != current.topMetaInkVisible) {
    render({48, 0, 48, 34});
  }
  if (previous.power != current.power || previous.elapsed != current.elapsed ||
      previous.navCursor != current.navCursor) {
    render({184, 0, 56, 34});
  }
  const bool contentRedrawn = previous.scrollOffset != current.scrollOffset;
  if (contentRedrawn) {
    render({0, 34, 240,
            static_cast<std::int16_t>(currentScene.bottomVisible ? 174
                                                                 : 206)});
  }
  if (!contentRedrawn && previous.name != current.name)
    render(contentRect(FieldDamageRect(42)));

  const RectI16 oldCursor = contentRect(ResolvedCursorRect(previous));
  const RectI16 newCursor = contentRect(ResolvedCursorRect(current));
  if (!contentRedrawn && (oldCursor != newCursor ||
                          previous.cursorInkVisible !=
                              current.cursorInkVisible)) {
    render(ExpandedCursorDamage(oldCursor));
    render(ExpandedCursorDamage(newCursor));
    render(contentRect(FieldDamageRect(42)));
    render(contentRect(FieldDamageRect(54)));
  }
  for (std::uint8_t index = 0; !contentRedrawn && index < current.fieldCount;
       ++index) {
    if (previous.fields[index] != current.fields[index]) {
      render(contentRect(FieldDamageRect(current.fields[index].y)));
    }
  }
  for (std::uint8_t index = 0;
       !contentRedrawn && index < current.operatorCount; ++index) {
    if (previous.operators[index] != current.operators[index]) {
      render(contentRect(
          FieldDamageRect(static_cast<std::int16_t>(144 + index * 9))));
    }
  }
  if (previous.cursor != current.cursor ||
      previous.selectedField != current.selectedField ||
      previous.selectedOperator != current.selectedOperator ||
      previous.nameAction != current.nameAction ||
      previous.adjustmentFocus != current.adjustmentFocus ||
      previous.adjustmentNote != current.adjustmentNote ||
      previous.adjustmentFineStep != current.adjustmentFineStep ||
      previous.adjustmentCoarseStep != current.adjustmentCoarseStep ||
      previous.fieldBottom != current.fieldBottom ||
      previous.fieldOptionCurrent != current.fieldOptionCurrent ||
      previous.trackNotes != current.trackNotes ||
      previous.selectedTrack != current.selectedTrack ||
      previous.bottomTrackVisualRect != current.bottomTrackVisualRect ||
      previous.bottomTrackVisualOverride != current.bottomTrackVisualOverride ||
      previous.bottomTrackInkVisible != current.bottomTrackInkVisible) {
    render({0, 208, 240, 32});
  }
  const auto operatorHeader = [](UiInstrumentCursor cursor) {
    return cursor == UiInstrumentCursor::Operator1
               ? 1
           : cursor == UiInstrumentCursor::Operator2
               ? 2
               : 0;
  };
  if (!contentRedrawn && operatorHeader(previous.cursor) !=
                             operatorHeader(current.cursor)) {
    render(contentRect(FieldDamageRect(132)));
  }
}

UiBuildStatus UiInstrumentView::Build(const UiInstrumentViewData &data,
                                      UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.contentOffsetY = UiVerticalList::Clamp(
      data.scrollOffset, BottomVisible(data) ? 208 : 240,
      ContentBottom(data));
  scene.topBackground = UiColorToken::SurfaceTopBar;
  scene.bottomBackground = UiColorToken::SurfaceBottomBar;

  const UiTopBarModel pageTop{
      .title = "INST",
      .meta = data.number,
      .elapsed = data.elapsed,
      .power = data.power,
      .navTarget = UiNavTarget::Instrument,
      .navCursor = data.navCursor,
      .metaSelectionRect = data.topMetaVisualRect,
      .metaSelectionOverride = data.topMetaVisualOverride,
      .metaInkVisible = data.topMetaInkVisible,
  };
  UiBottomBarModel bottom{.kind = UiBottomBarKind::Hidden};
  if (data.cursor == UiInstrumentCursor::Name) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"LOAD", "SAVE", "RENAME", {}};
    bottom.actions.count = 3;
    bottom.actions.active = std::min<std::uint8_t>(data.nameAction, 2);
  } else if (data.cursor == UiInstrumentCursor::Type) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kTypeOptions;
    bottom.selector.current = static_cast<std::uint8_t>(data.kind);
    bottom.selector.wrap = true;
  } else if (data.fieldBottom == UiInstrumentFieldBottom::Open) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"OPEN", {}, {}, {}};
    bottom.actions.count = 1;
  } else if (data.fieldBottom == UiInstrumentFieldBottom::Adjustment) {
    bottom.kind = UiBottomBarKind::AdjustmentLegend;
    bottom.adjustment = {.fineStep = data.adjustmentFineStep,
                         .coarseStep = data.adjustmentCoarseStep,
                         .coarseOctave = data.adjustmentNote,
                         .fineLabel = data.adjustmentNote
                                          ? std::string_view("NOTE")
                                          : std::string_view{},
                         .coarseLabel = data.adjustmentNote
                                            ? std::string_view("OCT")
                                            : std::string_view{}};
  } else if (data.fieldBottom == UiInstrumentFieldBottom::BooleanSelector) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kBooleanOptions;
    bottom.selector.current =
        std::min<std::uint8_t>(data.fieldOptionCurrent, 1U);
    bottom.selector.wrap = true;
  } else if (data.fieldBottom ==
             UiInstrumentFieldBottom::InterpolationSelector) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kInterpolationOptions;
    bottom.selector.current =
        std::min<std::uint8_t>(data.fieldOptionCurrent, 1U);
    bottom.selector.wrap = true;
  } else if (data.fieldBottom == UiInstrumentFieldBottom::LoopSelector) {
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = kLoopOptions;
    bottom.selector.current =
        std::min<std::uint8_t>(data.fieldOptionCurrent, 4U);
    bottom.selector.wrap = true;
  }
  UiTrackNotesModel tracks;
  tracks.notes = data.trackNotes;
  tracks.selectedTrack = data.selectedTrack;
  tracks.trackSelectionRect = data.bottomTrackVisualRect;
  tracks.trackSelectionOverride = data.bottomTrackVisualOverride;
  tracks.trackInkVisible = data.bottomTrackInkVisible;
  const UiAdjustmentLegendModel adjustment{
      .fineStep = data.adjustmentFineStep,
      .coarseStep = data.adjustmentCoarseStep,
      .coarseOctave = data.adjustmentNote,
      .fineLabel = data.adjustmentNote ? std::string_view("NOTE")
                                       : std::string_view{},
      .coarseLabel = data.adjustmentNote ? std::string_view("OCT")
                                         : std::string_view{},
  };
  const UiBarInputs inputs{
      .pageTop = pageTop,
      .pageDefault = bottom,
      .editHeldTracks = &tracks,
      .enterHeldAdjustment = data.adjustmentFocus ? &adjustment : nullptr,
      .editHeldNumber = data.numberFocus,
  };
  const UiResolvedChrome chrome = UiBarResolver::Resolve(inputs);
  const UiBuildStatus topStatus =
      UiChromeRenderer::BuildTop(chrome.top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  if (data.kind == UiInstrumentKind::Sid ||
      data.kind == UiInstrumentKind::Opal) {
    UiSceneBuilder<64, 256> topBuilder(scene.top);
    topBuilder.Text("EXPERIMENTAL", 62, 21, UiColorToken::SystemWarning);
    if (!topBuilder.Ok())
      return UiBuildStatus::CommandOverflow;
  }
  scene.bottomVisible = chrome.bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(chrome.bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  const UiColorToken nameColor =
      data.name == "--" ? UiColorToken::DerivedTextFaint : UiColorToken::TextNormal;
  builder.Text("NAME", 9, 42, UiColorToken::TextDim);
  builder.UserText(data.name, 92, 42, nameColor);
  DrawField(builder, "TYPE", TypeName(data.kind), 54,
            UiColorToken::TextNormal);

  if (data.kind == UiInstrumentKind::Opal) {
    DrawSection(builder, "GENERAL SETTINGS", 70);
    for (std::uint8_t index = 0; index < data.fieldCount; ++index) {
      DrawField(builder, data.fields[index].label, data.fields[index].value,
                data.fields[index].y,
                data.fields[index].value == "--" ? UiColorToken::DerivedTextFaint
                                                 : UiColorToken::TextNormal,
                data.fields[index].userData);
    }
    DrawSection(builder, "OPERATOR SETTINGS", 120);
    const bool operator2Focused =
        data.cursor == UiInstrumentCursor::Operator2;
    builder.Text("OP 1", 144, 132,
                 operator2Focused ? UiColorToken::TextDim
                                  : UiColorToken::TextColored);
    builder.Text("OP 2", 190, 132,
                 operator2Focused ? UiColorToken::TextColored
                                  : UiColorToken::TextDim);
    for (std::uint8_t index = 0; index < data.operatorCount; ++index) {
      const std::int16_t y = static_cast<std::int16_t>(144 + index * 9);
      builder.Text(data.operators[index].label, 9, y, UiColorToken::TextDim);
      builder.Text(data.operators[index].op1, 144, y,
                   UiColorToken::TextNormal);
      builder.Text(data.operators[index].op2, 190, y,
                   UiColorToken::TextNormal);
    }
  } else {
    for (std::uint8_t index = 0; index < data.fieldCount; ++index) {
      DrawField(builder, data.fields[index].label, data.fields[index].value,
                data.fields[index].y,
                data.fields[index].value == "--" ? UiColorToken::DerivedTextFaint
                                                 : UiColorToken::TextNormal);
    }
  }

  if (!data.numberFocus && data.cursor != UiInstrumentCursor::None) {
    const RectI16 cursor = ResolvedCursorRect(data);
    if (!cursor.Empty())
      builder.Selection(cursor);
    if (data.cursorInkVisible) {
      if (data.cursor == UiInstrumentCursor::Name) {
        builder.Text("NAME", 9, 42, UiColorToken::TextHighlighted);
        builder.UserText(data.name, 92, 42, UiColorToken::TextHighlighted);
      } else if (data.cursor == UiInstrumentCursor::Type) {
        builder.Text("TYPE", 9, 54, UiColorToken::TextHighlighted);
        builder.Text(TypeName(data.kind), 92, 54, UiColorToken::TextHighlighted);
      } else if (data.cursor == UiInstrumentCursor::Field &&
                 data.selectedField < data.fieldCount) {
        const UiInstrumentField &field = data.fields[data.selectedField];
        SelectedValueLayout layout;
        std::uint8_t textIndex = 0U;
        if (SelectedSubfield(data, layout, textIndex)) {
          builder.Text(layout.text.substr(textIndex, 1),
                       static_cast<std::int16_t>(
                           layout.x + textIndex * UiFont5x7::kAdvance),
                       layout.y, UiColorToken::TextHighlighted);
        } else {
          builder.Text(field.label, 9, field.y,
                       UiColorToken::TextHighlighted);
          if (field.userData)
            builder.UserText(field.value, 92, field.y,
                             UiColorToken::TextHighlighted);
          else
            builder.Text(field.value, 92, field.y,
                         UiColorToken::TextHighlighted);
        }
      } else if ((data.cursor == UiInstrumentCursor::Operator1 ||
                  data.cursor == UiInstrumentCursor::Operator2) &&
                 data.selectedOperator < data.operatorCount) {
        const std::int16_t y =
            static_cast<std::int16_t>(144 + data.selectedOperator * 9);
        const UiInstrumentOperatorRow &row =
            data.operators[data.selectedOperator];
        SelectedValueLayout layout;
        std::uint8_t textIndex = 0U;
        if (SelectedSubfield(data, layout, textIndex)) {
          builder.Text(layout.text.substr(textIndex, 1),
                       static_cast<std::int16_t>(
                           layout.x + textIndex * UiFont5x7::kAdvance),
                       layout.y, UiColorToken::TextHighlighted);
        } else if (data.cursor == UiInstrumentCursor::Operator1) {
          builder.Text(row.op1, 144, y, UiColorToken::TextHighlighted);
        } else {
          builder.Text(row.op2, 190, y, UiColorToken::TextHighlighted);
        }
      }
    }
  }

  return builder.Ok() ? UiBuildStatus::Built : UiBuildStatus::CommandOverflow;
}

} // namespace ui2
