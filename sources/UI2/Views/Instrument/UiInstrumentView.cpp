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
#include <span>

namespace ui2 {
namespace {
// Table headers align with TYPE's value; numeric cells sit under the middle
// letter.
constexpr std::array<std::int16_t, 4> kColumnHeaderX{92, 119, 146, 175};
constexpr std::array<std::string_view, 4> kDrumHeaders{"PIT", "TUN", "DEC",
                                                       "WAVE"};
constexpr std::array<std::string_view, 4> kSidEnvelopeHeaders{"ATK", "DEC",
                                                              "SUS", "REL"};
bool SidEnvelopeField(const UiInstrumentViewData &data, std::uint8_t index) {
  return data.kind == UiInstrumentKind::Sid && index == 5 &&
         index < data.fieldCount && data.fields[index].value.size() == 4;
}
constexpr std::array<std::string_view, 8> kDrumWaves{
    "P12.5", "P25", "P50", "TRI", "GB", "NES", "SN", "WHITE"};
unsigned DrumWaveIndex(std::string_view value) {
  if (value.size() < 4)
    return 0;
  const char c = value[3];
  return (c <= '9' ? c - '0' : c - 'A' + 10) & 7;
}
bool ColumnField(const UiInstrumentViewData &data, std::uint8_t index) {
  return (data.kind == UiInstrumentKind::Drum && index < 12 &&
          index < data.fieldCount && data.fields[index].value.size() >= 4) ||
         SidEnvelopeField(data, index);
}
bool ColumnCell(const UiInstrumentViewData &data) {
  return data.cursor == UiInstrumentCursor::Field &&
         ColumnField(data, data.selectedField);
}
std::int16_t ColumnValueX(UiInstrumentKind kind, unsigned col) {
  return kColumnHeaderX[col] +
         (kind == UiInstrumentKind::Drum && col == 3 ? 0 : 6);
}
std::string_view ColumnValue(UiInstrumentKind kind, std::string_view value,
                             unsigned col) {
  return kind == UiInstrumentKind::Drum && col == 3
             ? kDrumWaves[DrumWaveIndex(value)]
             : value.substr(col, 1);
}

constexpr std::array<std::string_view, kUiInstrumentTypeCount> kTypeOptions{
    "NONE",  "SAMPLE",   "MIDI",    "SID",      "OPAL",    "DRUM",
    "STACK", "CHIPTUNE", "GB-WAVE", "GB-PULSE", "GB-NOISE"};
constexpr std::array<std::string_view, 4> kGBDutyOptions{"12.5%", "25%", "50%",
                                                         "75%"};
constexpr std::array<std::string_view, 4> kGBWaveLevelOptions{"MUTE", "100%",
                                                              "50%", "25%"};
constexpr std::array<std::string_view, 8> kChiptuneWaveOptions{
    "PULSE 12.5", "PULSE 25",  "PULSE 50",      "TRIANGLE 4BIT",
    "NOISE GB7",  "NOISE NES", "NOISE SN76489", "WHITE NOISE"};
constexpr std::array<std::string_view, 7> kStackWaveOptions{
    "PULSE 12.5", "PULSE 25", "PULSE 50", "SAW", "TRIANGLE", "ORGAN", "VOX"};
constexpr std::array<std::string_view, 2> kBooleanOptions{"NO", "YES"};
constexpr std::array<std::string_view, 5> kSampleLoopOptions{
    "ONE SHOT", "FORWARD", "PING PONG", "OSCILLATOR", "LOOP SYNC"};
constexpr std::array<std::string_view, 3> kSampleFilterModeOptions{
    "ORIGINAL", "BASSY", "SCREAM"};
constexpr std::array<std::string_view, 2> kSampleInterpolationOptions{"LINEAR",
                                                                      "NONE"};
constexpr std::array<std::string_view, 9> kSidWaveformOptions{
    "--", "A", "/", "A/", "PULSE", "A PULSE", "/ PULSE", "A/ PULSE", "NOISE"};
constexpr std::array<std::string_view, 4> kSidFilterOptions{"LP", "BP", "HP",
                                                            "NOTCH"};
constexpr std::array<std::string_view, 2> kOpalAlgorithmOptions{"1*2", "1+2"};
constexpr std::array<std::string_view, 8> kOpalWaveOptions{
    "SINE", "HALF", "ABS", "PULS", "EVEN", "AB-E", "SQR", "DSQR"};
constexpr std::array<std::string_view, 4> kOpalKeyscaleOptions{"0", "1.5", "3",
                                                               "6"};

std::span<const std::string_view> OptionsFor(UiInstrumentFieldOptions options) {
  switch (options) {
  case UiInstrumentFieldOptions::Boolean:
    return kBooleanOptions;
  case UiInstrumentFieldOptions::SampleLoop:
    return kSampleLoopOptions;
  case UiInstrumentFieldOptions::SampleFilterMode:
    return kSampleFilterModeOptions;
  case UiInstrumentFieldOptions::SampleInterpolation:
    return kSampleInterpolationOptions;
  case UiInstrumentFieldOptions::SidWaveform:
    return kSidWaveformOptions;
  case UiInstrumentFieldOptions::SidFilter:
    return kSidFilterOptions;
  case UiInstrumentFieldOptions::OpalAlgorithm:
    return kOpalAlgorithmOptions;
  case UiInstrumentFieldOptions::OpalWave:
    return kOpalWaveOptions;
  case UiInstrumentFieldOptions::OpalKeyscale:
    return kOpalKeyscaleOptions;
  case UiInstrumentFieldOptions::ChiptuneWave:
    return kChiptuneWaveOptions;
  case UiInstrumentFieldOptions::StackWave:
    return kStackWaveOptions;
  case UiInstrumentFieldOptions::GBDuty:
    return kGBDutyOptions;
  case UiInstrumentFieldOptions::GBWaveLevel:
    return kGBWaveLevelOptions;
  case UiInstrumentFieldOptions::None:
    return {};
  }
  return {};
}

std::string_view TypeName(UiInstrumentKind kind) {
  return kTypeOptions[static_cast<std::size_t>(kind)];
}

unsigned FocusedOperator(const UiInstrumentViewData &data) {
  if (data.numberFocus || data.selectedOperator >= data.operatorCount)
    return 0;
  return data.cursor == UiInstrumentCursor::Operator1   ? 1
         : data.cursor == UiInstrumentCursor::Operator2 ? 2
                                                        : 0;
}

RectI16 ResolvedCursorRect(const UiInstrumentViewData &data) {
  if (data.cursorVisualOverride && !data.cursorVisualRect.Empty()) {
    // Instrument cursor animation runs in the scrollable content's logical
    // coordinate space. Tail fields can therefore sit below y=240 before the
    // content offset is applied. Clipping here would discard their selection
    // bubble before the renderer has a chance to translate it into view.
    return data.cursorVisualRect;
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
               std::string_view value, std::int16_t y, UiColorToken valueColor,
               bool userData = false) {
  builder.Text(label, 9, y, UiColorToken::TextDim);
  if (userData)
    builder.UserText(value, 92, y, valueColor);
  else
    builder.Text(value, 92, y, valueColor);
}

void DrawSection(UiSceneBuilder<256, 1024> &builder, std::string_view label,
                 std::int16_t y, std::int16_t right = 231) {
  const std::int16_t width = UiFont5x7::TextWidth(label.size());
  builder.Text(label, 9, y, UiColorToken::TextColored);
  builder.Fill({static_cast<std::int16_t>(9 + width + 7),
                static_cast<std::int16_t>(y + 3),
                static_cast<std::int16_t>(right - 16 - width), 1},
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
    if (ColumnCell(data)) {
      const auto col = std::min<unsigned>(data.selectedSubfield, 3);
      return {.text = ColumnValue(data.kind, field.value, col),
              .x = ColumnValueX(data.kind, col),
              .y = field.y};
    }
    return {
        .text = field.value, .x = 92, .y = field.y, .userData = field.userData};
  }
  if ((data.cursor == UiInstrumentCursor::Operator1 ||
       data.cursor == UiInstrumentCursor::Operator2) &&
      data.selectedOperator < data.operatorCount) {
    const UiInstrumentOperatorRow &row = data.operators[data.selectedOperator];
    return {.text = data.cursor == UiInstrumentCursor::Operator1 ? row.op1
                                                                 : row.op2,
            .x = static_cast<std::int16_t>(
                data.cursor == UiInstrumentCursor::Operator1 ? 144 : 190),
            .y = UiInstrumentOperatorRowY(data.selectedOperator)};
  }
  return {};
}

bool SelectedSubfield(const UiInstrumentViewData &data,
                      SelectedValueLayout &layout, std::uint8_t &textIndex) {
  if (ColumnCell(data)) {
    layout = SelectedValue(data);
    textIndex = 0;
    return !layout.text.empty();
  }
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
  return data.numberFocus || data.adjustmentFocus || ColumnCell(data) ||
         data.cursor == UiInstrumentCursor::Name ||
         data.cursor == UiInstrumentCursor::Type ||
         data.fieldBottom != UiInstrumentFieldBottom::Hidden;
}

} // namespace

RectI16 UiInstrumentView::CursorTargetRect(const UiInstrumentViewData &data) {
  SelectedValueLayout layout;
  std::uint8_t textIndex = 0U;
  if (SelectedSubfield(data, layout, textIndex)) {
    return {static_cast<std::int16_t>(layout.x +
                                      textIndex * UiFont5x7::kAdvance - 2),
            static_cast<std::int16_t>(layout.y - 1),
            static_cast<std::int16_t>(
                (ColumnCell(data) ? UiFont5x7::TextWidth(layout.text.size())
                                  : UiFont5x7::kGlyphWidth) +
                4),
            9};
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
      const std::int16_t y = static_cast<std::int16_t>(
          UiInstrumentOperatorRowY(data.selectedOperator) - 1);
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

std::int16_t UiInstrumentView::ContentBottom(const UiInstrumentViewData &data) {
  std::int16_t bottom = 63;
  for (std::uint8_t index = 0; index < data.fieldCount; ++index) {
    bottom =
        std::max(bottom, static_cast<std::int16_t>(data.fields[index].y + 8));
  }
  for (std::uint8_t index = 0; index < data.operatorCount; ++index) {
    bottom = std::max(
        bottom, static_cast<std::int16_t>(UiInstrumentOperatorRowY(index) + 7));
  }
  return bottom;
}

std::int16_t UiInstrumentView::RevealCursor(std::int16_t currentOffset,
                                            const UiInstrumentViewData &data) {
  const std::int16_t viewportBottom = BottomVisible(data) ? 208 : 240;
  auto target = CursorTargetRect(data);
  if (data.cursor == UiInstrumentCursor::Field &&
      data.selectedField < data.fieldCount) {
    for (const auto &section : UiInstrumentSections(data.kind)) {
      if (section.firstField != data.selectedField)
        continue;
      const auto sectionY =
          static_cast<std::int16_t>(data.fields[data.selectedField].y - 12);
      target.height += target.y - sectionY;
      target.y = sectionY;
      break;
    }
  }
  return UiVerticalList::Reveal(currentOffset, target, 34, viewportBottom,
                                ContentBottom(data));
}

RectI16 UiInstrumentView::FieldDamageRect(std::int16_t y) {
  // Translate scrollable content before clipping to the screen.
  return {5, static_cast<std::int16_t>(y - 1), 230, 11};
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
            static_cast<std::int16_t>(currentScene.bottomVisible ? 174 : 206)});
  }
  if (!contentRedrawn && previous.name != current.name)
    render(contentRect(FieldDamageRect(42)));
  const std::uint8_t columnHeaderField =
      current.kind == UiInstrumentKind::Sid ? 5 : 0;
  if (!contentRedrawn && ColumnField(current, columnHeaderField) &&
      (previous.selectedSubfield != current.selectedSubfield ||
       previous.cursor != current.cursor ||
       previous.selectedField != current.selectedField))
    render(
        contentRect(FieldDamageRect(current.fields[columnHeaderField].y - 12)));

  const RectI16 oldCursor = contentRect(ResolvedCursorRect(previous));
  const RectI16 newCursor = contentRect(ResolvedCursorRect(current));
  if (!contentRedrawn &&
      (oldCursor != newCursor ||
       previous.cursorInkVisible != current.cursorInkVisible)) {
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
  for (std::uint8_t index = 0; !contentRedrawn && index < current.operatorCount;
       ++index) {
    if (previous.operators[index] != current.operators[index]) {
      render(contentRect(FieldDamageRect(UiInstrumentOperatorRowY(index))));
    }
  }
  if ((ColumnCell(current) && previous.fields[current.selectedField] !=
                                  current.fields[current.selectedField]) ||
      previous.selectedSubfield != current.selectedSubfield ||
      previous.enterSubfieldFocus != current.enterSubfieldFocus ||
      previous.cursor != current.cursor ||
      previous.selectedField != current.selectedField ||
      previous.selectedOperator != current.selectedOperator ||
      previous.nameAction != current.nameAction ||
      previous.adjustmentFocus != current.adjustmentFocus ||
      previous.adjustmentNote != current.adjustmentNote ||
      previous.adjustmentFineStep != current.adjustmentFineStep ||
      previous.adjustmentCoarseStep != current.adjustmentCoarseStep ||
      previous.fieldBottom != current.fieldBottom ||
      previous.sampleLoaded != current.sampleLoaded ||
      previous.sampleImport != current.sampleImport ||
      previous.sampleRecord != current.sampleRecord ||
      previous.sampleAction != current.sampleAction ||
      previous.fieldOptionCurrent != current.fieldOptionCurrent ||
      previous.fieldOptions != current.fieldOptions ||
      previous.fieldOptionWrap != current.fieldOptionWrap ||
      previous.trackNotes != current.trackNotes ||
      previous.selectedTrack != current.selectedTrack ||
      previous.bottomTrackVisualRect != current.bottomTrackVisualRect ||
      previous.bottomTrackVisualOverride != current.bottomTrackVisualOverride ||
      previous.bottomTrackInkVisible != current.bottomTrackInkVisible) {
    render({0, 208, 240, 32});
  }
  if (!contentRedrawn &&
      FocusedOperator(previous) != FocusedOperator(current)) {
    render(contentRect(FieldDamageRect(kUiInstrumentOperatorHeaderY)));
  }
}

UiBuildStatus UiInstrumentView::Build(const UiInstrumentViewData &data,
                                      UiPalette &, UiFrameScene &scene) {
  scene.Clear();
  scene.topHeight = 34;
  scene.bottomTop = 208;
  scene.contentOffsetY = UiVerticalList::Clamp(
      data.scrollOffset, BottomVisible(data) ? 208 : 240, ContentBottom(data));
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
  } else if (ColumnCell(data)) {
    const auto col = std::min<unsigned>(data.selectedSubfield, 3);
    if (data.kind == UiInstrumentKind::Drum && col == 3 &&
        data.enterSubfieldFocus) {
      bottom.kind = UiBottomBarKind::Selector;
      bottom.selector.options = kDrumWaves;
      bottom.selector.current =
          DrumWaveIndex(data.fields[data.selectedField].value);
      bottom.selector.wrap = true;
    } else {
      bottom.kind = UiBottomBarKind::Actions;
      bottom.actions.actions = {"EDIT", {}, {}, {}};
      bottom.actions.count = 1;
    }
  } else if (data.enterSubfieldFocus) {
    // Packed hex and bit fields navigate a component horizontally;
    // UP/DOWN edits that component, matching the FX parameter interaction.
    bottom.kind = UiBottomBarKind::AdjustmentLegend;
    bottom.adjustment.fineLabel = "DIGIT";
    bottom.adjustment.coarseLabel = "VALUE";
  } else if (data.fieldBottom == UiInstrumentFieldBottom::SampleActions) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"LOAD", {}, {}, {}};
    bottom.actions.count = 1;
    if (data.sampleImport)
      bottom.actions.actions[bottom.actions.count++] = "IMPORT";
    if (data.sampleRecord)
      bottom.actions.actions[bottom.actions.count++] = "RECORD";
    if (data.sampleLoaded)
      bottom.actions.actions[bottom.actions.count++] = "EDIT";
    bottom.actions.active = data.sampleAction;
  } else if (data.fieldBottom == UiInstrumentFieldBottom::Edit) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"EDIT", {}, {}, {}};
    bottom.actions.count = 1;
  } else if (data.fieldBottom == UiInstrumentFieldBottom::Open) {
    bottom.kind = UiBottomBarKind::Actions;
    bottom.actions.actions = {"OPEN", {}, {}, {}};
    bottom.actions.count = 1;
  } else if (data.fieldBottom == UiInstrumentFieldBottom::Adjustment) {
    bottom.kind = UiBottomBarKind::AdjustmentLegend;
    bottom.adjustment = {
        .fineStep = data.adjustmentFineStep,
        .coarseStep = data.adjustmentCoarseStep,
        .coarseOctave = data.adjustmentNote,
        .fineLabel =
            data.adjustmentNote ? std::string_view("NOTE") : std::string_view{},
        .coarseLabel =
            data.adjustmentNote ? std::string_view("OCT") : std::string_view{},
        .showCoarse = data.adjustmentFocus};
  } else if (data.fieldBottom == UiInstrumentFieldBottom::Selector) {
    const std::span<const std::string_view> options =
        OptionsFor(data.fieldOptions);
    if (options.empty())
      return UiBuildStatus::DesignRequired;
    bottom.kind = UiBottomBarKind::Selector;
    bottom.selector.options = options;
    bottom.selector.current =
        std::min<std::uint8_t>(data.fieldOptionCurrent,
                               static_cast<std::uint8_t>(options.size() - 1U));
    bottom.selector.wrap = data.fieldOptionWrap;
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
      .hexadecimal = ColumnCell(data),
      .fineLabel =
          data.adjustmentNote ? std::string_view("NOTE") : std::string_view{},
      .coarseLabel =
          data.adjustmentNote ? std::string_view("OCT") : std::string_view{},
  };
  const UiBarInputs inputs{
      .pageTop = pageTop,
      .pageDefault = bottom,
      .enterHeldTracks = &tracks,
      .enterHeldAdjustment = data.adjustmentFocus ? &adjustment : nullptr,
      .enterHeldNumber = data.numberFocus,
  };
  const UiResolvedChrome chrome = UiBarResolver::Resolve(inputs);
  const UiBuildStatus topStatus =
      UiChromeRenderer::BuildTop(chrome.top, scene.top);
  if (topStatus != UiBuildStatus::Built)
    return topStatus;
  scene.bottomVisible = chrome.bottom.kind != UiBottomBarKind::Hidden;
  const UiBuildStatus bottomStatus =
      UiChromeRenderer::BuildBottom(chrome.bottom, scene.bottom);
  if (bottomStatus != UiBuildStatus::Built)
    return bottomStatus;

  UiSceneBuilder<256, 1024> builder(scene.content);
  const UiColorToken nameColor = data.name == "--"
                                     ? UiColorToken::DerivedTextFaint
                                     : UiColorToken::TextNormal;
  builder.Text("NAME", 9, 42, UiColorToken::TextDim);
  builder.UserText(data.name, 92, 42, nameColor);
  DrawField(builder, "TYPE", TypeName(data.kind), 54, UiColorToken::TextNormal);

  if (data.kind == UiInstrumentKind::Opal) {
    DrawSection(builder, "GENERAL SETTINGS", 70);
    for (std::uint8_t index = 0; index < data.fieldCount; ++index) {
      DrawField(builder, data.fields[index].label, data.fields[index].value,
                data.fields[index].y,
                data.fields[index].value == "--"
                    ? UiColorToken::DerivedTextFaint
                    : UiColorToken::TextNormal,
                data.fields[index].userData);
    }
    DrawSection(builder, "OPERATOR SETTINGS", kUiInstrumentOperatorHeaderY,
                136);
    const unsigned focusedOperator = FocusedOperator(data);
    builder.Text("OP 1", 144, kUiInstrumentOperatorHeaderY,
                 focusedOperator == 1 ? UiColorToken::TextColored
                                      : UiColorToken::TextDim);
    builder.Text("OP 2", 190, kUiInstrumentOperatorHeaderY,
                 focusedOperator == 2 ? UiColorToken::TextColored
                                      : UiColorToken::TextDim);
    for (std::uint8_t index = 0; index < data.operatorCount; ++index) {
      const std::int16_t y = UiInstrumentOperatorRowY(index);
      builder.Text(data.operators[index].label, 9, y, UiColorToken::TextDim);
      builder.Text(data.operators[index].op1, 144, y, UiColorToken::TextNormal);
      builder.Text(data.operators[index].op2, 190, y, UiColorToken::TextNormal);
    }
  } else {
    for (const auto &section : UiInstrumentSections(data.kind)) {
      if (section.firstField >= data.fieldCount)
        continue;
      const bool columns = ColumnField(data, section.firstField);
      const auto headerY =
          static_cast<std::int16_t>(data.fields[section.firstField].y - 12);
      DrawSection(builder, section.title, headerY, columns ? 84 : 231);
      if (columns) {
        const auto &headers = data.kind == UiInstrumentKind::Drum
                                  ? kDrumHeaders
                                  : kSidEnvelopeHeaders;
        for (unsigned col = 0; col < kColumnHeaderX.size(); ++col)
          builder.Text(headers[col], kColumnHeaderX[col], headerY,
                       ColumnCell(data) && data.selectedSubfield == col
                           ? UiColorToken::TextColored
                           : UiColorToken::TextDim);
      }
    }
    for (std::uint8_t index = 0; index < data.fieldCount; ++index) {
      if (ColumnField(data, index)) {
        const auto &field = data.fields[index];
        builder.Text(field.label, 9, field.y, UiColorToken::TextDim);
        for (unsigned col = 0; col < kColumnHeaderX.size(); ++col)
          builder.Text(ColumnValue(data.kind, field.value, col),
                       ColumnValueX(data.kind, col), field.y,
                       UiColorToken::TextNormal);
        continue;
      }
      DrawField(builder, data.fields[index].label, data.fields[index].value,
                data.fields[index].y,
                data.fields[index].value == "--"
                    ? UiColorToken::DerivedTextFaint
                    : UiColorToken::TextNormal,
                data.fields[index].userData);
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
        builder.Text(TypeName(data.kind), 92, 54,
                     UiColorToken::TextHighlighted);
      } else if (data.cursor == UiInstrumentCursor::Field &&
                 data.selectedField < data.fieldCount) {
        const UiInstrumentField &field = data.fields[data.selectedField];
        SelectedValueLayout layout;
        std::uint8_t textIndex = 0U;
        if (SelectedSubfield(data, layout, textIndex)) {
          builder.Text(ColumnCell(data) ? layout.text
                                        : layout.text.substr(textIndex, 1),
                       static_cast<std::int16_t>(
                           layout.x + textIndex * UiFont5x7::kAdvance),
                       layout.y, UiColorToken::TextHighlighted);
        } else {
          builder.Text(field.label, 9, field.y, UiColorToken::TextHighlighted);
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
        const std::int16_t y = UiInstrumentOperatorRowY(data.selectedOperator);
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
