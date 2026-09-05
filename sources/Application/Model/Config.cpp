/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Config.h"
#include "ThemeDocument.h"
using ThemeDocument::IsLegacyThemeColorId;
using ThemeDocument::kAllSemanticThemeColors;
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Externals/etl/include/etl/flat_map.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/string_utilities.h"
#include "Foundation/Variables/Variable.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/Console/nanoprintf.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include "ThemeConstants.h"
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <utility>

#define CONFIG_FILE_PATH "/.config.xml"
#define CONFIG_VERSION_NUMBER 4

#define MIDI_DEVICE_LEN 4

static const char *lineOutOptions[3] = {"HP Low", "HP High", "Line Level"};
static const char *midiDeviceList[MIDI_DEVICE_LEN] = {"OFF", "TRS", "USB",
                                                      "TRS+USB"};
static const char *midiSendSync[2] = {"Off", "Send"};
static const char *midiClockSyncOptions[2] = {"Internal", "External"};
static const char *importResamplerOptions[] = {"None", "Linear"};
static constexpr int kImportResamplerOptionCount = 2;
static const char *uiTextCaseOptions[] = {"Case", "CASE", "case"};
static constexpr int kUiTextCaseOptionCount = 3;

// NOTE: these MUST match the persisted RecordSource enum in
// Application/Audio/RecordingPlatform.h.
static const char *recordSourceOptions[3] = {"Line IN", "On board mic",
                                             "Headphone mic"};

// Param keys MUST fit in this length limit!
typedef etl::string<13> ParamString;

// Use default color values from ThemeConstants.h
// Other default values not related to theme colors:
constexpr int DEFAULT_LINEOUT = 0x2;
constexpr int DEFAULT_MIDIDEVICE = 0x0;
constexpr int DEFAULT_MIDISYNC = 0x0;
constexpr int DEFAULT_BACKLIGHT_LEVEL = 0xFF; // Default to max brightness (255)
constexpr int DEFAULT_REC_SOURCE = 0x0;
constexpr int DEFAULT_OUTPUT_VOLUME = 40;
constexpr int DEFAULT_IMPORT_RESAMPLER = 0; // default for picoTracker is none (as original)

// Use a struct to define parameter information
struct ConfigParam {
  const char *name;
  union {
    int intValue;
    const char *strValue;
  } defaultValue;
  FourCC::enum_type fourcc;
  const char **options;
  int optionCount;
  bool isString;
};

// Define parameters as a static array instead of a ETL flat_map for example,
// because using a flat_map static requires too much stack space for
// initialization
static const ConfigParam configParams[] = {
    // Color variables
    {"BACKGROUND",
     {.intValue = ThemeConstants::DEFAULT_BACKGROUND},
     FourCC::VarBGColor,
     nullptr,
     0,
     false},
    {"FOREGROUND",
     {.intValue = ThemeConstants::DEFAULT_FOREGROUND},
     FourCC::VarFGColor,
     nullptr,
     0,
     false},
    {"HICOLOR1",
     {.intValue = ThemeConstants::DEFAULT_HICOLOR1},
     FourCC::VarHI1Color,
     nullptr,
     0,
     false},
    {"HICOLOR2",
     {.intValue = ThemeConstants::DEFAULT_HICOLOR2},
     FourCC::VarHI2Color,
     nullptr,
     0,
     false},
    {"CONSOLECOLOR",
     {.intValue = ThemeConstants::DEFAULT_CONSOLECOLOR},
     FourCC::VarConsoleColor,
     nullptr,
     0,
     false},
    {"CURSORCOLOR",
     {.intValue = ThemeConstants::DEFAULT_CURSORCOLOR},
     FourCC::VarCursorColor,
     nullptr,
     0,
     false},
    {"INFOCOLOR",
     {.intValue = ThemeConstants::DEFAULT_INFOCOLOR},
     FourCC::VarInfoColor,
     nullptr,
     0,
     false},
    {"WARNCOLOR",
     {.intValue = ThemeConstants::DEFAULT_WARNCOLOR},
     FourCC::VarWarnColor,
     nullptr,
     0,
     false},
    {"ERRORCOLOR",
     {.intValue = ThemeConstants::DEFAULT_ERRORCOLOR},
     FourCC::VarErrorColor,
     nullptr,
     0,
     false},
    {"ACCENTCOLOR",
     {.intValue = ThemeConstants::DEFAULT_ACCENT},
     FourCC::VarAccentColor,
     nullptr,
     0,
     false},
    {"ACCENTALTCOLOR",
     {.intValue = ThemeConstants::DEFAULT_ACCENT_ALT},
     FourCC::VarAccentAltColor,
     nullptr,
     0,
     false},
    {"EMPHASISCOLOR",
     {.intValue = ThemeConstants::DEFAULT_EMPHASIS},
     FourCC::VarEmphasisColor,
     nullptr,
     0,
     false},

    // Device settings with options
    {"LINEOUT",
     {.intValue = DEFAULT_LINEOUT},
     FourCC::VarLineOut,
     lineOutOptions,
     3,
     false},
    {"MIDIDEVICE",
     {.intValue = DEFAULT_MIDIDEVICE},
     FourCC::VarMidiDevice,
     midiDeviceList,
     4,
     false},
    {"MIDISYNC",
     {.intValue = DEFAULT_MIDISYNC},
     FourCC::VarMidiSync,
     midiSendSync,
     2,
     false},
    {"UIFONT",
     {.intValue = ThemeConstants::DEFAULT_UIFONT},
     FourCC::VarUIFont,
     ThemeConstants::FONT_NAMES,
     ThemeConstants::FONT_COUNT,
     false},
    {"UITEXTCASE",
     {.intValue = 1},
     FourCC::VarUITextCase,
     uiTextCaseOptions,
     kUiTextCaseOptionCount,
     false},

    // {"RESERVED1", ThemeConstants::DEFAULT_RESERVED1,
    // FourCC::VarReserved1Color},
    // {"RESERVED2", ThemeConstants::DEFAULT_RESERVED2,
    // FourCC::VarReserved2Color},
    // {"RESERVED3", ThemeConstants::DEFAULT_RESERVED3,
    // FourCC::VarReserved3Color},
    // {"RESERVED4", ThemeConstants::DEFAULT_RESERVED4,
    // FourCC::VarReserved4Color},

    {"THEMENAME",
     {.strValue = ThemeConstants::DEFAULT_THEME_NAME},
     FourCC::VarThemeName,
     nullptr,
     0,
     true},

    // Display brightness setting
    {"BACKLIGHTLEVEL",
     {.intValue = DEFAULT_BACKLIGHT_LEVEL},
     FourCC::VarBacklightLevel,
     nullptr,
     0,
     false},
    {"OUTPUTVOLUME",
     {.intValue = DEFAULT_OUTPUT_VOLUME},
     FourCC::VarOutputVolume,
     nullptr,
     0,
     false},
    {"IMPORTRESAMP",
     {.intValue = DEFAULT_IMPORT_RESAMPLER},
     FourCC::VarImportResampler,
     importResamplerOptions,
     kImportResamplerOptionCount,
     false},

    {"RECORDSOURCE",
     {.intValue = 0},
     FourCC::VarRecordSource,
     recordSourceOptions,
     3,
     false},
};

Config::Config()
    : VariableContainer(&variables_),
      background_(FourCC::VarBGColor,
                  static_cast<int>(ThemeConstants::DEFAULT_BACKGROUND)),
      foreground_(FourCC::VarFGColor,
                  static_cast<int>(ThemeConstants::DEFAULT_FOREGROUND)),
      hiColor1_(FourCC::VarHI1Color,
                static_cast<int>(ThemeConstants::DEFAULT_HICOLOR1)),
      hiColor2_(FourCC::VarHI2Color,
                static_cast<int>(ThemeConstants::DEFAULT_HICOLOR2)),
      consoleColor_(FourCC::VarConsoleColor,
                    static_cast<int>(ThemeConstants::DEFAULT_CONSOLECOLOR)),
      cursorColor_(FourCC::VarCursorColor,
                   static_cast<int>(ThemeConstants::DEFAULT_CURSORCOLOR)),
      infoColor_(FourCC::VarInfoColor,
                 static_cast<int>(ThemeConstants::DEFAULT_INFOCOLOR)),
      warnColor_(FourCC::VarWarnColor,
                 static_cast<int>(ThemeConstants::DEFAULT_WARNCOLOR)),
      errorColor_(FourCC::VarErrorColor,
                  static_cast<int>(ThemeConstants::DEFAULT_ERRORCOLOR)),
      accentColor_(FourCC::VarAccentColor,
                   static_cast<int>(ThemeConstants::DEFAULT_ACCENT)),
      accentAltColor_(FourCC::VarAccentAltColor,
                      static_cast<int>(ThemeConstants::DEFAULT_ACCENT_ALT)),
      emphasisColor_(FourCC::VarEmphasisColor,
                     static_cast<int>(ThemeConstants::DEFAULT_EMPHASIS)),
      lineOut_(FourCC::VarLineOut, lineOutOptions, 3, DEFAULT_LINEOUT),
      midiDevice_(FourCC::VarMidiDevice, midiDeviceList, 4, DEFAULT_MIDIDEVICE),
      midiSync_(FourCC::VarMidiSync, midiSendSync, 2, DEFAULT_MIDISYNC),
      importResampler_(FourCC::VarImportResampler, importResamplerOptions,
                       kImportResamplerOptionCount, DEFAULT_IMPORT_RESAMPLER),
      uiFont_(FourCC::VarUIFont, ThemeConstants::FONT_NAMES,
              ThemeConstants::FONT_COUNT, ThemeConstants::DEFAULT_UIFONT),
      uiTextCase_(FourCC::VarUITextCase, uiTextCaseOptions,
                  kUiTextCaseOptionCount, 1),
      themeName_(FourCC::VarThemeName, ThemeConstants::DEFAULT_THEME_NAME),
      backlightLevel_(FourCC::VarBacklightLevel, DEFAULT_BACKLIGHT_LEVEL),
      outputVolume_(FourCC::VarOutputVolume, DEFAULT_OUTPUT_VOLUME),
      recordSource_(FourCC::VarRecordSource, recordSourceOptions, 3, 0) {

  variables_.push_back(&background_);
  variables_.push_back(&foreground_);
  variables_.push_back(&hiColor1_);
  variables_.push_back(&hiColor2_);
  variables_.push_back(&consoleColor_);
  variables_.push_back(&cursorColor_);
  variables_.push_back(&infoColor_);
  variables_.push_back(&warnColor_);
  variables_.push_back(&errorColor_);
  variables_.push_back(&accentColor_);
  variables_.push_back(&accentAltColor_);
  variables_.push_back(&emphasisColor_);
  variables_.push_back(&lineOut_);
  variables_.push_back(&midiDevice_);
  variables_.push_back(&midiSync_);
  variables_.push_back(&importResampler_);
  variables_.push_back(&uiFont_);
  variables_.push_back(&uiTextCase_);
  variables_.push_back(&themeName_);
  variables_.push_back(&backlightLevel_);
  variables_.push_back(&outputVolume_);
  variables_.push_back(&recordSource_);

  PersistencyDocument doc;

  if (!doc.Load(CONFIG_FILE_PATH)) {
    Trace::Error("CONFIG Could not open file for reading: %s",
                 CONFIG_FILE_PATH);
    Save(); // and write the defaults to SDCard
    return;
  }

  bool elem = doc.FirstChild();
  if (!elem || strcmp(doc.ElemName(), "CONFIG")) {
    Trace::Log("CONFIG", "Bad config.xml format!");
    doc.Close();
    useDefaultConfig();
    Save();
    return;
  }
  int configVersion = 0;
  bool validVersionAttribute = false;
  bool invalidRootAttribute = false;
  char currentConfigVersion[12]{};
  npf_snprintf(currentConfigVersion, sizeof(currentConfigVersion), "%d",
               CONFIG_VERSION_NUMBER);
  while (doc.NextAttribute()) {
    if (!validVersionAttribute && strcmp(doc.attrname_, "VERSION") == 0 &&
        strcmp(doc.attrval_, currentConfigVersion) == 0) {
      configVersion = CONFIG_VERSION_NUMBER;
      validVersionAttribute = true;
    } else {
      invalidRootAttribute = true;
    }
  }
  // Device config is deliberately not a migration surface. A schema mismatch
  // discards the whole file so stale UI1 palette fields (or any other legacy
  // representation) can never leak into the UI2 runtime.
  if (!validVersionAttribute || invalidRootAttribute ||
      configVersion != CONFIG_VERSION_NUMBER) {
    Trace::Log("CONFIG", "Discarding unsupported config version %d",
               configVersion);
    doc.Close();
    useDefaultConfig();
    if (!Save())
      Trace::Error("CONFIG: Failed to replace unsupported config");
    return;
  }
  elem = doc.r_ == YXML_ELEMSTART
             ? true
             : doc.FirstChild(); // now get first child element of CONFIG
  std::uint32_t semanticThemeColorMask = 0U;
  while (elem) {
    // Check if the parameter exists in our parameters list
    bool paramFound = false;
    for (const auto &param : configParams) {
      if (strcmp(doc.ElemName(), param.name) == 0) {
        paramFound = true;
        break;
      }
    }

    // Special handling for Color elements
    if (strcmp(doc.ElemName(), "Color") == 0) {
      // Process Color element
      ReadColorVariable(&doc);
      elem = doc.NextSibling();
      continue;
    }

    if (strcmp(doc.ElemName(), "UiColor") == 0) {
      semanticThemeColorMask |= ReadSemanticThemeColor(&doc);
      elem = doc.NextSibling();
      continue;
    }

    if (!paramFound) {
      Trace::Log("CONFIG", "Found unknown config parameter \"%s\", skipping...",
                 doc.ElemName());
      elem = doc.NextSibling();
      continue;
    }
    bool hasAttr = doc.NextAttribute();
    while (hasAttr) {
      // Special handling for Theme Name sadly because it is a string and no
      // easy way to look that that up in configParams data above
      if (!strcmp(doc.ElemName(), "THEMENAME")) {
        if (Variable *themeVar = FindVariable(FourCC::VarThemeName)) {
          themeVar->SetString(doc.attrval_);
          Trace::Log("CONFIG", "Read Theme Name:%s", doc.attrval_);
        }
      } else {
        // Find the variable by name in configParams
        for (const auto &param : configParams) {
          if (!strcmp(doc.ElemName(), param.name)) {
            if (Variable *var = FindVariable(param.fourcc)) {
              var->SetInt(atoi(doc.attrval_));
              Trace::Log("CONFIG", "Set %s = %s", param.name, doc.attrval_);
            }
            break;
          }
        }
      }
      hasAttr = doc.NextAttribute();
    }
    elem = doc.NextSibling();
  }
  doc.Close();
  if (semanticThemeColorMask != kAllSemanticThemeColors) {
    Trace::Error("CONFIG: Discarding incomplete current config");
    useDefaultConfig();
    if (!Save())
      Trace::Error("CONFIG: Failed to replace incomplete config");
    return;
  }
  Trace::Log("CONFIG", "Loaded successfully");
}

Config::~Config() {}

void Config::ResetSemanticThemeColors() {
  semanticThemeColors_ = DefaultSemanticThemeColors();
}

void Config::useDefaultConfig() {
  for (Variable *variable : variables_)
    variable->Reset();
  ResetSemanticThemeColors();
}

bool Config::Save() {
  auto fs = FileSystem::GetInstance();
  auto fp = fs->Open(CONFIG_FILE_PATH, "w");
  if (!fp) {
    Trace::Error("Could not open file for writing: %s", CONFIG_FILE_PATH);
    return false;
  }
  Trace::Log("PERSISTENCYSERVICE", "Opened Proj File: %s", CONFIG_FILE_PATH);
  tinyxml2::XMLPrinter printer(fp.get());

  SaveContent(&printer);

  return fp->Sync();
}

// Write color variables to an XMLPrinter using the same format as in
// SaveContent
void Config::SaveContent(tinyxml2::XMLPrinter *printer) {
  // Log the number of variables in the list before saving
  Trace::Log("CONFIG", "Saving %d variables to config file", variables_.size());

  // store config version
  printer->OpenElement("CONFIG");
  printer->PushAttribute("VERSION", CONFIG_VERSION_NUMBER);
  // save all of the config parameters
  auto it = variables_.begin();
  for (size_t i = 0; i < variables_.size(); i++) {
    Variable *var = *it;
    FourCC id = var->GetID();

    // Skip color variables as they will be handled by WriteColorVariables
    if (IsLegacyThemeColorId(id) ||
        id.get_enum() == FourCC::VarReserved1Color ||
        id.get_enum() == FourCC::VarReserved2Color ||
        id.get_enum() == FourCC::VarReserved3Color ||
        id.get_enum() == FourCC::VarReserved4Color) {
      it++;
      continue;
    }

    etl::string<16> elemName = var->GetName();
    to_upper_case(elemName);

    printer->OpenElement(elemName.c_str());
    // these settings need to be saved as the Int values not as String
    // values hence we *dont* use GetString() !
    if (var->GetType() == Variable::CHAR_LIST) {
      char buf[16];
      npf_snprintf(buf, sizeof(buf), "%d", var->GetInt());
      printer->PushAttribute("VALUE", buf);
    } else {
      // all other settings need to be saved as thier String values
      printer->PushAttribute("VALUE", var->GetString().c_str());
    }
    printer->CloseElement();
    it++;
  }

  // Write color variables using the dedicated method
  WriteColorVariables(printer);
  WriteSemanticThemeColors(printer);

  printer->CloseElement();
  Trace::Log("CONFIG", "Saved config");
};

int Config::GetValue(const char *key) {
  Variable *v = FindVariable(key);
  if (v) {
    Trace::Log("CONFIG", "Got value for %s=%s", key, v->GetString().c_str());
  } else {
    Trace::Log("CONFIG", "No value for requested key:%s", key);
  }
  return v ? v->GetInt() : 0;
};
