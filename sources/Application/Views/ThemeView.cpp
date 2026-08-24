/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "ThemeView.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/UI2/Ui2SettingsControllerAdapters.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/RenameModalView.h"
#include "Application/Views/ModalDialogs/TextInputModalView.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include <Application/Model/ThemeConstants.h>
#include <array>
#include <cstdio>
#include <stdint.h>

#define FONT_FIELD_LINE 6

#define COLOR_LABEL_WIDTH 12
#define COMPONENT_SPACING 3

constexpr uint8_t COLOR_COMPONENT_X_COL_POS[COLOR_COMPONENT_COUNT] = {16, 8, 0};
constexpr uint8_t COLOR_COMPONENT_X_OFFSETS[COLOR_COMPONENT_COUNT] = {
    COLOR_LABEL_WIDTH, COLOR_LABEL_WIDTH + COMPONENT_SPACING,
    COLOR_LABEL_WIDTH + 2 * COMPONENT_SPACING};

namespace {

bool ThemeColorForUi2(Config *config, FourCC id, std::uint32_t &color) {
  Variable *value = config == nullptr ? nullptr : config->FindVariable(id);
  if (value == nullptr)
    return false;
  color = static_cast<std::uint32_t>(value->GetInt()) & 0x00FFFFFFU;
  return true;
}

// A legacy file has twelve named colors. This is an explicit compatibility
// projection: repeated semantic slots remain mirrors of their legacy source on
// every capture. ThemeViewUi2Snapshot::editableColorMask separately records
// which UI2 rows this legacy controller can address directly.
bool CaptureSemanticColors(
    Config *config,
    std::array<std::uint32_t, ThemeViewUi2Snapshot::ColorCount> &colors) {
  std::uint32_t foreground = 0;
  std::uint32_t background = 0;
  std::uint32_t console = 0;
  std::uint32_t cursor = 0;
  std::uint32_t info = 0;
  std::uint32_t warning = 0;
  std::uint32_t error = 0;
  std::uint32_t emphasis = 0;
  std::uint32_t highlighted = 0;
  std::uint32_t accentAlt = 0;
  std::uint32_t accent = 0;
  const bool valid =
      ThemeColorForUi2(config, FourCC::VarFGColor, foreground) &&
      ThemeColorForUi2(config, FourCC::VarBGColor, background) &&
      ThemeColorForUi2(config, FourCC::VarConsoleColor, console) &&
      ThemeColorForUi2(config, FourCC::VarCursorColor, cursor) &&
      ThemeColorForUi2(config, FourCC::VarInfoColor, info) &&
      ThemeColorForUi2(config, FourCC::VarWarnColor, warning) &&
      ThemeColorForUi2(config, FourCC::VarErrorColor, error) &&
      ThemeColorForUi2(config, FourCC::VarEmphasisColor, emphasis) &&
      ThemeColorForUi2(config, FourCC::VarHI2Color, highlighted) &&
      ThemeColorForUi2(config, FourCC::VarAccentAltColor, accentAlt) &&
      ThemeColorForUi2(config, FourCC::VarAccentColor, accent);

  colors = {
      background, // surface.bg
      console,    // surface.top_bar
      console,    // surface.bottom_bar
      foreground, // text.normal
      emphasis,   // text.dim
      background, // text.highlighted
      highlighted, // text.colored
      cursor,      // cursor.primary
      accentAlt,   // cursor.row
      accent,      // playback.active
      info,       // system.info
      warning,    // system.warning
      error,      // system.error
      foreground, // battery.normal
      info,       // battery.charging
      error,      // battery.low
      info,       // vu.safe
      warning,    // vu.warning
      error,      // vu.peak
  };
  return valid;
}

} // namespace

ThemeView::ThemeView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), themeNameVar_(FourCC::ActionThemeName,
                                        ThemeConstants::DEFAULT_THEME_NAME) {

  auto config = Config::GetInstance();

  // Get the current theme name from Config
  Variable *configThemeVar = config->FindVariable(FourCC::VarThemeName);
  etl::string<MAX_THEME_NAME_LENGTH> currentThemeName = "default";

  // If the theme name is set in the config, use it
  if (configThemeVar && !configThemeVar->GetString().empty()) {
    currentThemeName = configThemeVar->GetString();
  }

  // Create the label and default value as variables to avoid temporary objects
  auto label = etl::string<MAX_UITEXTFIELD_LABEL_LENGTH>("Theme: ");
  auto defaultValue = etl::string<MAX_THEME_NAME_LENGTH>(currentThemeName);

  themeNameVar_.SetString(currentThemeName.c_str(), false);

  // Theme name at y=3.
  GUIPoint position = GetAnchor();
  textFields_.emplace_back(themeNameVar_, position, label,
                           FourCC::ActionThemeName, defaultValue);
  themeNameField_ = &(*textFields_.rbegin());
  themeNameField_->AddObserver(*this);
  fieldList_.insert(fieldList_.end(), themeNameField_);

  // Initialize the edit mode flag
  themeNameEditMode_ = false;

  // Initialize the export theme name
  exportThemeName_ = currentThemeName;

  // Import / Export at y=4.
  GUIPoint actionPos = position;
  actionPos._y += 1;

  actionField_.emplace_back("Import", FourCC::ActionImport, actionPos);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  actionPos._x += 8;
  actionField_.emplace_back("Export", FourCC::ActionExport, actionPos);
  fieldList_.insert(fieldList_.end(), &(*actionField_.rbegin()));
  (*actionField_.rbegin()).AddObserver(*this);

  // Font at y=6.
  position._y = FONT_FIELD_LINE;
  Variable *fontVar = config->FindVariable(FourCC::VarUIFont);
  intVarField_.emplace_back(position, *fontVar, "Font: %s", 0,
                            ThemeConstants::FONT_COUNT - 1, 1,
                            ThemeConstants::FONT_COUNT - 1);
  fieldList_.insert(fieldList_.end(), &(*intVarField_.rbegin()));
  (*intVarField_.rbegin()).AddObserver(*this);

  // Foreground color
  position._y = 9;
  addColorField("Foreground", config->FindVariable(FourCC::VarFGColor),
                CD_NORMAL, position);

  // Background color
  position._y += 1;
  addColorField("Background", config->FindVariable(FourCC::VarBGColor),
                CD_BACKGROUND, position);

  // Highlight color
  position._y += 1;
  addColorField("Highlight1", config->FindVariable(FourCC::VarHI1Color),
                CD_HILITE1, position);

  // Highlight2 color
  position._y += 1;
  addColorField("Highlight2", config->FindVariable(FourCC::VarHI2Color),
                CD_HILITE2, position);

  // Console color
  position._y += 1;
  addColorField("Console", config->FindVariable(FourCC::VarConsoleColor),
                CD_CONSOLE, position);

  // Cursor color
  position._y += 1;
  addColorField("Cursor", config->FindVariable(FourCC::VarCursorColor),
                CD_CURSOR, position);

  // Info color
  position._y += 1;
  addColorField("Info", config->FindVariable(FourCC::VarInfoColor), CD_INFO,
                position);

  // Warning color
  position._y += 1;
  addColorField("Warning", config->FindVariable(FourCC::VarWarnColor), CD_WARN,
                position);

  // Error color
  position._y += 1;
  addColorField("Error", config->FindVariable(FourCC::VarErrorColor), CD_ERROR,
                position);

  // Play color
  position._y += 1;
  addColorField("Accent", config->FindVariable(FourCC::VarAccentColor),
                CD_ACCENT, position);

  // Mute color
  position._y += 1;
  addColorField("AccentAlt", config->FindVariable(FourCC::VarAccentAltColor),
                CD_ACCENTALT, position);

  // Emphasis color
  position._y += 1;
  addColorField("Emphasis", config->FindVariable(FourCC::VarEmphasisColor),
                CD_EMPHASIS, position);
}

ThemeView::~ThemeView() {}

ThemeViewUi2Snapshot ThemeView::SnapshotForUi2() const {
  ThemeViewUi2Snapshot snapshot;
  const auto name = themeNameField_->GetString();
  std::snprintf(snapshot.name.data(), snapshot.name.size(), "%s",
                name.c_str());
  snapshot.colorsValid =
      CaptureSemanticColors(Config::GetInstance(), snapshot.colors);
  snapshot.editableColorMask = ui2::kLegacyThemeEditableColorMask;
  snapshot.nameActionMask = ui2::kLegacyThemeNameActionMask;

  const ui2::UiThemeControllerFocus focus =
      ui2::AdaptLegacyThemeFocus(static_cast<std::int16_t>(GetFocusIndex()));
  snapshot.focus = focus.focus;
  snapshot.selectedColor = focus.selectedColor;
  snapshot.nameAction = focus.nameAction;
  return snapshot;
}

FontViewUi2Snapshot ThemeView::FontSnapshotForUi2() const {
  FontViewUi2Snapshot snapshot;
  Variable *font = Config::GetInstance()->FindVariable(FourCC::VarUIFont);
  if (font == nullptr || font->GetType() != Variable::CHAR_LIST)
    return snapshot;

  snapshot.count = font->GetListSize();
  const int selected = font->GetInt();
  if (selected >= 0 && selected < snapshot.count)
    snapshot.current = static_cast<std::uint8_t>(selected);
  const char *const *options = font->GetListPointer();
  const char *value = options != nullptr && snapshot.current < snapshot.count
                          ? options[snapshot.current]
                          : "";
  std::snprintf(snapshot.font.data(), snapshot.font.size(), "%s", value);
  return snapshot;
}

void ThemeView::Reset() {
  exportThemeName_.clear();
  themeNameEditMode_ = false;
  _forceRedraw = false;
  configDirty_ = false;
}

void ThemeView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed)
    return;

  if (mask == EPBM_ENTER && GetFocus() == themeNameField_) {
    const auto currentName = themeNameField_->GetString();
    DoModal(RenameModalView::Create(*this, currentName.c_str(),
                                    MAX_THEME_NAME_LENGTH),
            ModalViewCallback::create<ThemeView,
                                      &ThemeView::onRenameFinished>(*this));
    isDirty_ = true;
    return;
  }

  if (!(GetFocus() == themeNameField_ &&
        (mask & (EPBM_ENTER | EPBM_EDIT))))
    FieldView::ProcessButtonMask(mask, pressed);

  if (mask & EPBM_NAV) {
    if (mask & EPBM_LEFT) {
      // Go back to Device view with NAV+LEFT
      ViewType vt = VT_DEVICE;
      ViewEvent ve(VET_SWITCH_VIEW, &vt);
      SetChanged();
      NotifyObservers(&ve);
    }
  } else if (mask & EPBM_PLAY) {
    Player *player = Player::GetInstance();
    player->OnStartButton(PM_SONG, viewData_->songX_, false, viewData_->songX_);
  }
}

void ThemeView::onRenameFinished(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() != RenameModalView::SaveReturnCode)
    return;
  const auto &rename = static_cast<const RenameModalView &>(dialog);
  if (rename.Value()[0] == '\0')
    return;
  exportThemeName_ = rename.Value();
  themeNameVar_.SetString(exportThemeName_.c_str(), false);
  themeNameField_->SetVariable(themeNameVar_);
  Config *config = Config::GetInstance();
  if (Variable *name = config->FindVariable(FourCC::VarThemeName))
    name->SetString(exportThemeName_.c_str());
  configDirty_ = true;
  isDirty_ = true;
}

void ThemeView::DrawView() {
  Clear();

  GUITextProperties props;
  GUIPoint pos = GetTitlePosition();

  // Draw title
  char titleString[SCREEN_WIDTH];
  strcpy(titleString, "Theme Settings");

  SetColor(CD_NORMAL);
  DrawString(pos._x, pos._y, titleString, props);

  // bit of a hack needed for font change as going from "standard" to "bold"
  // will leave behind partial characters due to different width of those string
  // labels
  DrawString(5, FONT_FIELD_LINE, "                            ", props);

  FieldView::Redraw();

  // just draw the RGB column headings directly:
  GUITextProperties headerProps;
  DrawString(13, 8, "R  G  B", headerProps);
}

void ThemeView::addSwatchField(ColorDefinition color, GUIPoint position) {
  position._x += 22;
  swatchField_.emplace_back(position, color);
  fieldList_.insert(fieldList_.end(), &(*swatchField_.rbegin()));
}

void ThemeView::addColorField(const char *label, Variable *colorVar,
                              ColorDefinition color, GUIPoint position) {

  staticField_.emplace_back(position, label);
  UIStaticField &labelField = *staticField_.rbegin();
  fieldList_.insert(fieldList_.end(), &labelField);

  uint32_t colorValue = static_cast<uint32_t>(colorVar->GetInt());

  for (uint8_t i = 0; i < COLOR_COMPONENT_COUNT; ++i) {
    GUIPoint componentPosition = position;
    componentPosition._x += COLOR_COMPONENT_X_OFFSETS[i];

    colorComponentVars_.emplace_back(
        colorVar->GetID(),
        static_cast<int>((colorValue >> COLOR_COMPONENT_X_COL_POS[i]) &
                         static_cast<uint32_t>(0xFF)));
    Variable &componentVar = *colorComponentVars_.rbegin();

    // bigger steps and set limits because we use RGB565 for colors
    if (i == 0 || i == 2) {
      intVarField_.emplace_back(componentPosition, componentVar, "%2.2X", 0,
                                248, 8, 16, 0);
    } else {
      intVarField_.emplace_back(componentPosition, componentVar, "%2.2X", 0,
                                252, 4, 16, 0);
    }
    UIIntVarField &componentField = *intVarField_.rbegin();
    fieldList_.insert(fieldList_.end(), &componentField);
    componentField.AddObserver(*this);

    colorComponentFields_.emplace_back();
    auto &fieldInfo = colorComponentFields_.back();
    fieldInfo.observable = &componentField;
    fieldInfo.componentVar = &componentVar;
    fieldInfo.colorVar = colorVar;
    fieldInfo.shift = COLOR_COMPONENT_X_COL_POS[i];
  }

  addSwatchField(color, position);
}

ThemeView::ColorComponentField *
ThemeView::findColorComponentField(Observable *observable) {
  for (auto &entry : colorComponentFields_) {
    if (entry.observable == observable) {
      return &entry;
    }
  }
  return nullptr;
}

void ThemeView::syncColorComponentVars(Variable *colorVar) {
  if (colorVar == nullptr) {
    return;
  }
  uint32_t colorValue = static_cast<uint32_t>(colorVar->GetInt());
  for (auto &entry : colorComponentFields_) {
    if (entry.colorVar != colorVar) {
      continue;
    }
    uint32_t componentValue =
        (colorValue >> entry.shift) & static_cast<uint32_t>(0xFF);
    entry.componentVar->SetInt(static_cast<int>(componentValue), false);

    if (entry.colorVar->GetID() == FourCC::VarBGColor) {
      // If the background or foreground color changed, we need to force a
      // redraw to update all the colors on the screen
      _forceRedraw = true;
    }
  }
}

void ThemeView::syncFieldsFromConfig() {
  // Get the current theme name from Config
  Config *config = Config::GetInstance();
  Variable *themeNameVar = config->FindVariable(FourCC::VarThemeName);

  if (themeNameVar && !themeNameVar->GetString().empty()) {
    // Get the theme name from Config
    etl::string<MAX_THEME_NAME_LENGTH> themeName = themeNameVar->GetString();

    // Update the theme name field
    themeNameVar_.SetString(themeName.c_str());
    themeNameField_->SetVariable(themeNameVar_);
    exportThemeName_ = themeName;
  }

  for (auto &entry : colorComponentFields_) {
    if (entry.colorVar == nullptr || entry.componentVar == nullptr) {
      continue;
    }

    uint32_t colorValue = entry.colorVar->GetInt();
    uint32_t componentValue =
        (colorValue >> entry.shift) & static_cast<uint32_t>(0xFF);
    entry.componentVar->SetInt(componentValue, false);
  }
}

void ThemeView::Update(Observable &o, I_ObservableData *d) {
  if (!hasFocus_) {
    return;
  }
  UIField *focus = GetFocus();
  focus->ClearFocus();
  focus->Draw(w_);
  w_.Flush();
  focus->SetFocus();
  focus->Draw(w_);
  isDirty_ = true;

  uintptr_t fourcc = (uintptr_t)d;

  ColorComponentField *componentField = findColorComponentField(&o);
  if (componentField != nullptr) {
    Variable *colorVar = componentField->colorVar;
    uint32_t colorValue = colorVar->GetInt();
    uint32_t newComponentValue = componentField->componentVar->GetInt() & 0xFF;
    colorValue &= ~(static_cast<uint32_t>(0xFF) << componentField->shift);
    colorValue |= newComponentValue << componentField->shift;
    colorVar->SetInt(static_cast<int>(colorValue));
    syncColorComponentVars(colorVar);
    fourcc = colorVar->GetID();
  }

  switch (fourcc) {
  // Handle theme import action
  case FourCC::ActionImport: {
    // Switch to the ThemeImportView
    ViewType vt = VT_THEME_IMPORT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
    return;
  }
  // Handle theme export action
  case FourCC::ActionExport: {
    // Get the theme name from the text field
    exportThemeName_ = themeNameField_->GetString();

    // Check if the theme name is empty
    if (exportThemeName_.empty()) {
      exportThemeName_ = ThemeConstants::DEFAULT_THEME_NAME;
      themeNameVar_.SetString(exportThemeName_.c_str());
      themeNameField_->SetVariable(themeNameVar_);
    }

    // Export the theme
    handleThemeExport();
    return;
  }
  // Handle theme name field
  case FourCC::ActionThemeName: {
    // Update the export theme name
    exportThemeName_ = themeNameField_->GetString();

    // Update the theme name in the Config
    Config *config = Config::GetInstance();
    Variable *themeNameVar = config->FindVariable(FourCC::VarThemeName);
    if (themeNameVar) {
      themeNameVar->SetString(exportThemeName_.c_str());
      configDirty_ = true;
    }
    themeNameVar_.SetString(exportThemeName_.c_str());
    return;
  }
  // if font changes call redraw all fields
  case FourCC::VarUIFont: {
    // need to force redraw of entire screen to update for font change
    ForceClear();
    DrawView();
    configDirty_ = true;
    break;
  }
  // Handle color variable changes
  case FourCC::VarBGColor:
  case FourCC::VarFGColor:
  case FourCC::VarHI1Color:
  case FourCC::VarHI2Color:
  case FourCC::VarConsoleColor:
  case FourCC::VarCursorColor:
  case FourCC::VarInfoColor:
  case FourCC::VarWarnColor:
  case FourCC::VarErrorColor:
  case FourCC::VarAccentColor:
  case FourCC::VarAccentAltColor:
  case FourCC::VarEmphasisColor:
    // case FourCC::VarReserved1Color:
    // case FourCC::VarReserved2Color:
    // case FourCC::VarReserved3Color:
    // case FourCC::VarReserved4Color:
    {
      // Update the AppWindow's color values from Config
      ((AppWindow &)w_).UpdateColorsFromConfig();

      // Force a redraw of the entire screen to update all colors
      _forceRedraw = true;
      configDirty_ = true;
      break;
    }
  default:
    NInvalid;
    break;
  };
}

void ThemeView::handleThemeExport() {
  // Check if the theme name is valid
  if (exportThemeName_.empty()) {
    exportThemeName_ = ThemeConstants::DEFAULT_THEME_NAME;
  }

  // Build the path to check if the theme already exists
  char pathBuffer[MAX_THEME_EXPORT_PATH_LENGTH + 1];
  memset(pathBuffer, 0, sizeof(pathBuffer));

  strcpy(pathBuffer, THEMES_DIR);
  strcat(pathBuffer, "/");
  strcat(pathBuffer, exportThemeName_.c_str());
  strcat(pathBuffer, THEME_FILE_EXTENSION);

  // Check if theme exists
  auto fs = FileSystem::GetInstance();
  if (fs->exists(pathBuffer)) {
    // Theme exists, ask for confirmation
    MessageBox *mb = MessageBox::Create(*this, "Theme already exists",
                                        "     Overwrite?", MBBF_YES | MBBF_NO);

    DoModal(mb, ModalViewCallback::create<ThemeView,
                                          &ThemeView::onConfirmThemeOverwrite>(
                    *this));
  } else {
    // Theme doesn't exist, export directly
    exportThemeWithName(exportThemeName_.c_str(), false);
  }
}

void ThemeView::onConfirmThemeOverwrite(View &, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    exportThemeWithName(exportThemeName_.c_str(), true);
  }
}

void ThemeView::exportThemeWithName(const char *themeName, bool overwrite) {
  // Export the theme using Config
  Config *config = Config::GetInstance();
  bool result = config->ExportTheme(themeName, overwrite);

  if (result) {
    // Update the theme name in the Config
    Variable *themeNameVar = config->FindVariable(FourCC::VarThemeName);
    if (themeNameVar) {
      themeNameVar->SetString(themeName);
      configDirty_ = true;
    }

    // Update the theme name field
    themeNameVar_.SetString(themeName);
    themeNameField_->SetVariable(themeNameVar_);
    exportThemeName_ = themeName;
  }

  // Show result message
  MessageBox *resultMb = MessageBox::Create(
      *this, result ? "Theme exported successfully " : "Failed to export theme",
      MBBF_OK);
  DoModal(resultMb);
}

void ThemeView::OnFocus() {
  // Refresh local field state from Config when returning from theme import.
  syncFieldsFromConfig();
  _forceRedraw = true;
  isDirty_ = true;
}

void ThemeView::OnFocusLost() {
  if (!configDirty_) {
    return;
  }

  Config *config = Config::GetInstance();
  if (!config->Save()) {
    Trace::Error("THEMEVIEW", "Failed to save theme config on focus lost");
    return;
  }

  Trace::Log("THEMEVIEW", "Saved theme config on focus lost");
  configDirty_ = false;
}

// Keep this method for backward compatibility
void ThemeView::exportTheme() {
  // This now just calls handleThemeExport
  handleThemeExport();
}

// We've replaced the static callbacks with lambdas and direct methods

void ThemeView::importTheme() {
  // Switch to the theme import view
  ViewType vt = VT_THEME_IMPORT;
  ViewEvent ve(VET_SWITCH_VIEW, &vt);
  SetChanged();
  NotifyObservers(&ve);
}
void ThemeView::AnimationUpdate() {
  if (_forceRedraw) {
    ForceClear();
    DrawView();
    _forceRedraw = false;
  }
  GUITextProperties props;
  drawBattery(props);
  drawPowerButtonUI(props);
}
