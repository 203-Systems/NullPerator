/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Config.h"
#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Externals/etl/include/etl/flat_map.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/string_utilities.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/Console/nanoprintf.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/I_File.h"
#include "ThemeConstants.h"
#include "Foundation/Variables/Variable.h"
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <utility>

#define CONFIG_FILE_PATH "/.config.xml"
#define CONFIG_VERSION_NUMBER 4
#define NPT_VERSION_NUMBER 1

#define MIDI_DEVICE_LEN 4

static const char *lineOutOptions[3] = {"HP Low", "HP High", "Line Level"};
static const char *midiDeviceList[MIDI_DEVICE_LEN] = {"OFF", "TRS", "USB",
                                                      "TRS+USB"};
static const char *midiSendSync[2] = {"Off", "Send"};
static const char *midiClockSyncOptions[2] = {"Internal", "External"};
static const char *remoteUIOnOff[2] = {"Off", "On"};
static const char *importResamplerOptions[] = {"None", "Linear"};
static constexpr int kImportResamplerOptionCount = 2;
static const char *uiTextCaseOptions[] = {"Case", "CASE", "case"};
static constexpr int kUiTextCaseOptionCount = 3;

// NOTE: these MUST match the persisted RecordSource enum in
// Application/Audio/RecordingPlatform.h.
static const char *recordSourceOptions[4] = {"All Off", "Line In", "Mic",
                                             "USB In"};

// Param keys MUST fit in this length limit!
typedef etl::string<13> ParamString;

namespace {

constexpr std::array<const char *, Config::SemanticThemeColorCount>
    kSemanticThemeColorKeys{{
        "surface.bg",       "surface.top_bar", "surface.bottom_bar",
        "text.normal",      "text.dim",        "text.highlighted",
        "text.colored",     "cursor.primary",  "cursor.row",
        "playback.active",  "system.info",     "system.warning",
        "system.error",     "battery.normal",  "battery.charging",
        "battery.low",      "vu.safe",         "vu.warning",
        "vu.peak",
    }};

constexpr std::uint32_t kAllSemanticThemeColors =
    (std::uint32_t{1} << Config::SemanticThemeColorCount) - 1U;

constexpr std::array<FourCC::enum_type, 12U> kLegacyThemeColorIds{{
    FourCC::VarBGColor,       FourCC::VarFGColor,
    FourCC::VarHI1Color,      FourCC::VarHI2Color,
    FourCC::VarConsoleColor,  FourCC::VarCursorColor,
    FourCC::VarInfoColor,     FourCC::VarWarnColor,
    FourCC::VarErrorColor,    FourCC::VarAccentColor,
    FourCC::VarAccentAltColor, FourCC::VarEmphasisColor,
}};

struct ThemeLoadState {
  Config::SemanticThemeColors semanticColors{};
  std::uint32_t semanticMask = 0U;
  int font = 0;
  bool hasFont = false;
};

bool ParseThemeColor(const char *text, std::uint32_t &color) {
  if (text == nullptr || text[0] == '\0')
    return false;
  const char *cursor = text[0] == '#' ? text + 1 : text;
  const std::size_t length = strlen(cursor);
  if (length == 0U || length > 6U)
    return false;

  std::uint32_t parsed = 0U;
  for (; *cursor != '\0'; ++cursor) {
    const unsigned char raw = static_cast<unsigned char>(*cursor);
    if (!std::isxdigit(raw))
      return false;
    parsed <<= 4U;
    if (raw >= '0' && raw <= '9')
      parsed |= static_cast<std::uint32_t>(raw - '0');
    else
      parsed |= static_cast<std::uint32_t>(std::toupper(raw) - 'A' + 10);
  }
  color = parsed;
  return true;
}

bool ParseThemeFont(const char *text, int &font) {
  if (text == nullptr || text[0] == '\0' || text[1] != '\0')
    return false;
  unsigned int parsed = 0U;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9')
      return false;
    parsed = parsed * 10U + static_cast<unsigned int>(*cursor - '0');
    if (parsed >= static_cast<unsigned int>(ThemeConstants::FONT_COUNT))
      return false;
  }
  font = static_cast<int>(parsed);
  return true;
}

bool IsLegacyThemeColorId(FourCC id) {
  for (const FourCC::enum_type candidate : kLegacyThemeColorIds) {
    if (id.get_enum() == candidate)
      return true;
  }
  return false;
}

int SemanticThemeColorIndex(const char *key) {
  if (key == nullptr)
    return -1;
  for (std::size_t index = 0U; index < kSemanticThemeColorKeys.size(); ++index) {
    if (std::strcmp(key, kSemanticThemeColorKeys[index]) == 0)
      return static_cast<int>(index);
  }
  return -1;
}

bool ReadFontElement(PersistencyDocument &doc, ThemeLoadState &state) {
  if (state.hasFont)
    return false;
  bool hasValue = false;
  int parsed = 0;
  while (doc.NextAttribute()) {
    if (std::strcmp(doc.attrname_, "value") != 0 || hasValue ||
        !ParseThemeFont(doc.attrval_, parsed)) {
      return false;
    }
    hasValue = true;
  }
  if (doc.HadError() || doc.r_ != YXML_ELEMEND || !hasValue)
    return false;
  state.font = parsed;
  state.hasFont = true;
  return true;
}

bool ReadSemanticThemeColorElement(PersistencyDocument &doc,
                                   ThemeLoadState &state) {
  const char *key = nullptr;
  const char *value = nullptr;
  char keyStorage[64]{};
  char valueStorage[64]{};
  while (doc.NextAttribute()) {
    if (std::strcmp(doc.attrname_, "key") == 0 && key == nullptr) {
      std::memcpy(keyStorage, doc.attrval_, strlen(doc.attrval_) + 1U);
      key = keyStorage;
    } else if (std::strcmp(doc.attrname_, "value") == 0 && value == nullptr) {
      std::memcpy(valueStorage, doc.attrval_, strlen(doc.attrval_) + 1U);
      value = valueStorage;
    } else {
      return false;
    }
  }
  if (doc.HadError() || doc.r_ != YXML_ELEMEND || key == nullptr ||
      value == nullptr)
    return false;
  const int index = SemanticThemeColorIndex(key);
  std::uint32_t parsed = 0U;
  if (index < 0 || !ParseThemeColor(value, parsed) ||
      (state.semanticMask & (std::uint32_t{1} << index)) != 0U) {
    return false;
  }
  state.semanticColors[static_cast<std::size_t>(index)] = parsed;
  state.semanticMask |= std::uint32_t{1} << index;
  return true;
}

bool ParseThemeDocument(PersistencyDocument &doc, ThemeLoadState &state) {
  if (!doc.FirstChild() || std::strcmp(doc.ElemName(), "NPT") != 0)
    return false;

  bool hasVersion = false;
  bool hasMagic = false;
  while (doc.NextAttribute()) {
    if (std::strcmp(doc.attrname_, "MAGIC") == 0 && !hasMagic &&
        std::strcmp(doc.attrval_, "NPT") == 0) {
      hasMagic = true;
    } else if (std::strcmp(doc.attrname_, "VERSION") == 0 && !hasVersion &&
               std::strcmp(doc.attrval_, "1") == 0) {
      hasVersion = true;
    } else {
      return false;
    }
  }
  if (!hasMagic || !hasVersion || doc.HadError())
    return false;
  bool child = doc.r_ == YXML_ELEMSTART;
  if (!child && doc.r_ != YXML_ELEMEND)
    child = doc.FirstChild();
  while (child) {
    const char *element = doc.ElemName();
    bool valid = false;
    if (std::strcmp(element, "Font") == 0)
      valid = ReadFontElement(doc, state);
    else if (std::strcmp(element, "UiColor") == 0)
      valid = ReadSemanticThemeColorElement(doc, state);
    if (!valid || doc.HadError())
      return false;
    child = doc.NextSibling();
  }

  // Streaming yxml only reports an unclosed root at EOF. Every theme load,
  // including validation of an export temp file, must therefore drain and
  // finish the document before it can affect the live Config.
  return !doc.HadError() && doc.Finish() && state.hasFont &&
         state.semanticMask == kAllSemanticThemeColors;
}

void ApplyThemeState(Config &config, const ThemeLoadState &state) {
  if (Variable *font = config.FindVariable(FourCC::VarUIFont))
    font->SetInt(state.font);
  config.SetSemanticThemeColors(state.semanticColors);
}

bool ValidateThemeFile(const char *path) {
  PersistencyDocument document;
  ThemeLoadState staged;
  return document.Load(path) && ParseThemeDocument(document, staged);
}

bool IsSafeThemeName(const char *themeName) {
  if (themeName == nullptr)
    return false;
  const std::size_t length = strlen(themeName);
  if (length == 0U || length > MAX_THEME_NAME_LENGTH)
    return false;
  return std::strchr(themeName, '/') == nullptr &&
         std::strchr(themeName, '\\') == nullptr &&
         std::strcmp(themeName, ".") != 0 && std::strcmp(themeName, "..") != 0;
}

struct ThemeExportPaths {
  static constexpr std::size_t BasePathCapacity =
      MAX_THEME_NAME_LENGTH + sizeof(THEMES_DIR) - 1U + 1U +
      sizeof(THEME_FILE_EXTENSION) - 1U;
  etl::string<BasePathCapacity> target;
  etl::string<BasePathCapacity + 5U> temporary;
  etl::string<BasePathCapacity + 5U> backup;
};

bool BuildThemeExportPaths(const char *themeName, ThemeExportPaths &paths) {
  if (!IsSafeThemeName(themeName))
    return false;
  paths.target = THEMES_DIR;
  paths.target.append("/");
  paths.target.append(themeName);
  paths.target.append(THEME_FILE_EXTENSION);

  paths.temporary = THEMES_DIR;
  paths.temporary.append("/.");
  paths.temporary.append(themeName);
  paths.temporary.append(THEME_FILE_EXTENSION);
  paths.temporary.append(".tmp");

  paths.backup = THEMES_DIR;
  paths.backup.append("/.");
  paths.backup.append(themeName);
  paths.backup.append(THEME_FILE_EXTENSION);
  paths.backup.append(".bak");
  return !paths.target.is_truncated() && !paths.temporary.is_truncated() &&
         !paths.backup.is_truncated();
}

bool RestoreThemeBackup(FileSystem &fileSystem,
                        const ThemeExportPaths &paths) {
  if (!fileSystem.exists(paths.backup.c_str()))
    return false;
  if (fileSystem.MoveFile(paths.backup.c_str(), paths.target.c_str()))
    return true;
  if (fileSystem.exists(paths.target.c_str()) &&
      !fileSystem.DeleteFile(paths.target.c_str())) {
    return false;
  }
  return fileSystem.MoveFile(paths.backup.c_str(), paths.target.c_str());
}

bool RecoverThemeExportJournal(FileSystem &fileSystem,
                               const ThemeExportPaths &paths) {
  const bool hasTarget = fileSystem.exists(paths.target.c_str());
  const bool hasTemporary = fileSystem.exists(paths.temporary.c_str());
  const bool hasBackup = fileSystem.exists(paths.backup.c_str());
  if (!hasTemporary && !hasBackup)
    return true;

  if (hasTarget && hasBackup) {
    if (!ValidateThemeFile(paths.target.c_str())) {
      // The replacement did not become a complete theme. The backup is the
      // only authoritative old byte stream; never discard it while restoring.
      if (!RestoreThemeBackup(fileSystem, paths))
        return false;
    } else if (!fileSystem.DeleteFile(paths.backup.c_str())) {
      Trace::Error("CONFIG: Theme backup cleanup deferred");
    }
    if (fileSystem.exists(paths.temporary.c_str()) &&
        !fileSystem.DeleteFile(paths.temporary.c_str())) {
      Trace::Error("CONFIG: Theme temp cleanup deferred");
    }
    return true;
  }

  if (!hasTarget && hasBackup) {
    if (!RestoreThemeBackup(fileSystem, paths))
      return false;
    if (hasTemporary && !fileSystem.DeleteFile(paths.temporary.c_str()))
      Trace::Error("CONFIG: Theme temp cleanup deferred");
    return true;
  }

  if (hasTarget) {
    // A temp without a backup means the stable target was never moved.
    if (hasTemporary && !fileSystem.DeleteFile(paths.temporary.c_str()))
      Trace::Error("CONFIG: Theme temp cleanup deferred");
    return true;
  }

  // A brand-new export can lose power after syncing its temp but before the
  // install rename. Install only a fully parseable temp; otherwise leave no
  // visible corrupt theme behind.
  if (hasTemporary && ValidateThemeFile(paths.temporary.c_str()) &&
      fileSystem.MoveFile(paths.temporary.c_str(), paths.target.c_str())) {
    return true;
  }
  if (hasTemporary && !fileSystem.DeleteFile(paths.temporary.c_str()))
    Trace::Error("CONFIG: Invalid theme temp cleanup deferred");
  return !fileSystem.exists(paths.temporary.c_str());
}

} // namespace

// Use default color values from ThemeConstants.h
// Other default values not related to theme colors:
constexpr int DEFAULT_LINEOUT = 0x2;
constexpr int DEFAULT_MIDIDEVICE = 0x0;
constexpr int DEFAULT_MIDISYNC = 0x0;
constexpr int DEFAULT_REMOTEUI = 0x1;
constexpr int DEFAULT_BACKLIGHT_LEVEL = 0xFF; // Default to max brightness (255)
constexpr int DEFAULT_REC_SOURCE = 0x0;
constexpr int DEFAULT_RECORD_LINE_GAIN_DB = 0;
constexpr int DEFAULT_RECORD_MIC_GAIN_DB = 0;
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
    {"REMOTEUI",
     {.intValue = DEFAULT_REMOTEUI},
     FourCC::VarRemoteUI,
     remoteUIOnOff,
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
     {.intValue = 1},
     FourCC::VarRecordSource,
     recordSourceOptions,
     4,
     false},
    {"RECORDLINEGAIN",
     {.intValue = DEFAULT_RECORD_LINE_GAIN_DB},
     FourCC::VarRecordLineGain,
     nullptr,
     0,
     false},
    {"RECORDMICGAIN",
     {.intValue = DEFAULT_RECORD_MIC_GAIN_DB},
     FourCC::VarRecordMicGain,
     nullptr,
     0,
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
      remoteUI_(FourCC::VarRemoteUI, remoteUIOnOff, 2, DEFAULT_REMOTEUI),
      importResampler_(FourCC::VarImportResampler, importResamplerOptions,
                       kImportResamplerOptionCount, DEFAULT_IMPORT_RESAMPLER),
      uiFont_(FourCC::VarUIFont, ThemeConstants::FONT_NAMES,
              ThemeConstants::FONT_COUNT, ThemeConstants::DEFAULT_UIFONT),
      uiTextCase_(FourCC::VarUITextCase, uiTextCaseOptions,
                  kUiTextCaseOptionCount, 1),
      themeName_(FourCC::VarThemeName, ThemeConstants::DEFAULT_THEME_NAME),
      backlightLevel_(FourCC::VarBacklightLevel, DEFAULT_BACKLIGHT_LEVEL),
      outputVolume_(FourCC::VarOutputVolume, DEFAULT_OUTPUT_VOLUME),
      recordSource_(FourCC::VarRecordSource, recordSourceOptions, 4, 1),
      recordLineGain_(FourCC::VarRecordLineGain, DEFAULT_RECORD_LINE_GAIN_DB),
      recordMicGain_(FourCC::VarRecordMicGain, DEFAULT_RECORD_MIC_GAIN_DB) {

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
  variables_.push_back(&remoteUI_);
  variables_.push_back(&importResampler_);
  variables_.push_back(&uiFont_);
  variables_.push_back(&uiTextCase_);
  variables_.push_back(&themeName_);
  variables_.push_back(&backlightLevel_);
  variables_.push_back(&outputVolume_);
  variables_.push_back(&recordSource_);
  variables_.push_back(&recordLineGain_);
  variables_.push_back(&recordMicGain_);

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
void Config::WriteColorVariables(tinyxml2::XMLPrinter *printer) {
  auto it = variables_.begin();
  for (size_t i = 0; i < variables_.size(); i++) {
    Variable *var = *it;
    FourCC id = var->GetID();

    // Check if this is a color variable
    if (IsLegacyThemeColorId(id)) {

      // Open a Color element
      printer->OpenElement("Color");

      // Add name attribute
      printer->PushAttribute("name", var->GetName());

      // Format color value in hex format with # prefix
      char hexValue[16];
      npf_snprintf(hexValue, sizeof(hexValue), "#%X", var->GetInt());

      // Add value attribute in hex format
      printer->PushAttribute("value", hexValue);

      // Close the Color element
      printer->CloseElement();
    }
    it++;
  }
}

void Config::WriteSemanticThemeColors(tinyxml2::XMLPrinter *printer) {
  for (std::size_t index = 0; index < semanticThemeColors_.size(); ++index) {
    printer->OpenElement("UiColor");
    printer->PushAttribute("key", kSemanticThemeColorKeys[index]);
    char value[8]{};
    npf_snprintf(value, sizeof(value), "#%06X",
                 static_cast<unsigned int>(semanticThemeColors_[index] &
                                           0x00FFFFFFU));
    printer->PushAttribute("value", value);
    printer->CloseElement();
  }
}

std::uint32_t Config::ReadSemanticThemeColor(PersistencyDocument *doc) {
  if (doc == nullptr || strcmp(doc->ElemName(), "UiColor") != 0)
    return 0U;

  char key[32]{};
  char value[16]{};
  while (doc->NextAttribute()) {
    if (strcmp(doc->attrname_, "key") == 0) {
      std::snprintf(key, sizeof(key), "%s", doc->attrval_);
    } else if (strcmp(doc->attrname_, "value") == 0) {
      std::snprintf(value, sizeof(value), "%s", doc->attrval_);
    }
  }
  std::uint32_t parsed = 0U;
  if (!ParseThemeColor(value, parsed))
    return 0U;
  for (std::size_t index = 0; index < kSemanticThemeColorKeys.size(); ++index) {
    if (strcmp(key, kSemanticThemeColorKeys[index]) != 0)
      continue;
    semanticThemeColors_[index] = parsed;
    return std::uint32_t{1} << index;
  }
  return 0U;
}

void Config::ReadColorVariable(PersistencyDocument *doc) {
  // Process the current element if it's a Color element
  if (strcmp(doc->ElemName(), "Color") == 0) {
    // Process Color element
    char colorName[64] = {0};
    char colorValue[64] = {0};

    // Get the name and value attributes
    while (doc->NextAttribute()) {
      if (strcmp(doc->attrname_, "name") == 0) {
        // Use safer string copy to ensure null-termination
        size_t len = strlen(doc->attrval_);
        if (len >= sizeof(colorName)) {
          len = sizeof(colorName) - 1; // Truncate if too long
        }
        memcpy(colorName, doc->attrval_, len);
        colorName[len] = '\0'; // Ensure null-termination
      } else if (strcmp(doc->attrname_, "value") == 0) {
        // Use safer string copy to ensure null-termination
        size_t len = strlen(doc->attrval_);
        if (len >= sizeof(colorValue)) {
          len = sizeof(colorValue) - 1; // Truncate if too long
        }
        memcpy(colorValue, doc->attrval_, len);
        colorValue[len] = '\0'; // Ensure null-termination
      }
    }

    // If we have both name and value, set the variable
    if (colorName[0] != '\0' && colorValue[0] != '\0') {
      std::uint32_t parsed = 0U;
      const bool parsedSuccessfully = ParseThemeColor(colorValue, parsed);
      const int value = static_cast<int>(parsed);

      if (parsedSuccessfully) {
        // Find the variable by name and set its value
        FourCC::enum_type fourcc = FourCC::Default;
        bool recognized = true;

        // Only support uppercase color names for consistency
        if (strcmp(colorName, "BACKGROUND") == 0) {
          fourcc = FourCC::VarBGColor;
        } else if (strcmp(colorName, "FOREGROUND") == 0) {
          fourcc = FourCC::VarFGColor;
        } else if (strcmp(colorName, "HICOLOR1") == 0) {
          fourcc = FourCC::VarHI1Color;
        } else if (strcmp(colorName, "HICOLOR2") == 0) {
          fourcc = FourCC::VarHI2Color;
        } else if (strcmp(colorName, "CONSOLECOLOR") == 0) {
          fourcc = FourCC::VarConsoleColor;
        } else if (strcmp(colorName, "CURSORCOLOR") == 0) {
          fourcc = FourCC::VarCursorColor;
        } else if (strcmp(colorName, "INFOCOLOR") == 0) {
          fourcc = FourCC::VarInfoColor;
        } else if (strcmp(colorName, "WARNCOLOR") == 0) {
          fourcc = FourCC::VarWarnColor;
        } else if (strcmp(colorName, "ERRORCOLOR") == 0) {
          fourcc = FourCC::VarErrorColor;
        } else if (strcmp(colorName, "ACCENTCOLOR") == 0) {
          fourcc = FourCC::VarAccentColor;
        } else if (strcmp(colorName, "ACCENTALTCOLOR") == 0) {
          fourcc = FourCC::VarAccentAltColor;
        } else if (strcmp(colorName, "EMPHASISCOLOR") == 0) {
          fourcc = FourCC::VarEmphasisColor;
        } else {
          recognized = false;
        }
        //  else if (strcmp(colorName, "RESERVED1") == 0) {
        //   fourcc = FourCC::VarReserved1Color;
        // } else if (strcmp(colorName, "RESERVED2") == 0) {
        //   fourcc = FourCC::VarReserved2Color;
        // } else if (strcmp(colorName, "RESERVED3") == 0) {
        //   fourcc = FourCC::VarReserved3Color;
        // } else if (strcmp(colorName, "RESERVED4") == 0) {
        //   fourcc = FourCC::VarReserved4Color;
        // }

        if (recognized) {
          Variable *var = FindVariable(fourcc);
          if (var) {
            var->SetInt(value);
            Trace::Log("CONFIG", "Read Color: %s = %d", colorName, value);
          }
        }
      }
    }
  }
}

bool Config::SaveTheme(tinyxml2::XMLPrinter *printer, const char *themeName) {
  Trace::Log("CONFIG", "Saving theme content to XML");

  printer->OpenElement("NPT");
  printer->PushAttribute("MAGIC", "NPT");
  printer->PushAttribute("VERSION", NPT_VERSION_NUMBER);

  // We don't need to save the theme name in the file
  // The filename itself serves as the theme name

  // Save the font setting
  Variable *fontVar = FindVariable(FourCC::VarUIFont);
  if (fontVar) {
    printer->OpenElement("Font");
    char buf[16];
    npf_snprintf(buf, sizeof(buf), "%d", fontVar->GetInt());
    printer->PushAttribute("value", buf);
    printer->CloseElement(); // Font
  }

  // NPT stores only the semantic UI2 palette. Legacy UI1 Color fields are
  // intentionally neither exported nor accepted by the NPT parser.
  WriteSemanticThemeColors(printer);

  // Close the THEME root element
  printer->CloseElement(); // NPT

  return true;
}

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

bool Config::LoadTheme(PersistencyDocument *doc) {
  Trace::Log("CONFIG", "Loading theme content from XML");
  if (doc == nullptr) {
    Trace::Error("CONFIG: Missing theme document");
    return false;
  }
  ThemeLoadState staged;
  if (!ParseThemeDocument(*doc, staged)) {
    Trace::Error("CONFIG: Theme document is incomplete or invalid");
    return false;
  }
  ApplyThemeState(*this, staged);
  return true;
}

int Config::GetValue(const char *key) {
  Variable *v = FindVariable(key);
  if (v) {
    Trace::Log("CONFIG", "Got value for %s=%s", key, v->GetString().c_str());
  } else {
    Trace::Log("CONFIG", "No value for requested key:%s", key);
  }
  return v ? v->GetInt() : 0;
};

bool Config::ExportTheme(const char *themeName, bool overwrite) {
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr)
    return false;

  // Create themes directory if it doesn't exist
  if (!fs->exists(THEMES_DIR)) {
    Trace::Error("Expected themes directory doesn't exist!");
    return false;
  }

  ThemeExportPaths paths;
  if (!BuildThemeExportPaths(themeName, paths) ||
      !RecoverThemeExportJournal(*fs, paths)) {
    Trace::Error("CONFIG: Theme export journal recovery failed");
    return false;
  }

  // Check if the file already exists and we're not overwriting
  const bool targetExisted = fs->exists(paths.target.c_str());
  if (targetExisted && !overwrite) {
    Trace::Error("Theme file already exists: %s", paths.target.c_str());
    return false;
  }

  // Write and validate a hidden sibling before moving the old file. Validation
  // catches adapters that report a short write without setting Error().
  auto fp = fs->Open(paths.temporary.c_str(), "w");
  if (!fp) {
    Trace::Error("Failed to open theme temp for writing: %s",
                 paths.temporary.c_str());
    return false;
  }
  bool serialized = false;
  {
    tinyxml2::XMLPrinter printer(fp.get());
    serialized = SaveTheme(&printer, themeName);
  }
  const bool synced = serialized && fp->Sync() && fp->Error() == 0;
  I_File *rawFile = AcquireLegacyFileHandle_DO_NOT_USE(std::move(fp));
  const bool closed = CloseFile_DO_NOT_USE(rawFile);
  if (!synced || !closed || !ValidateThemeFile(paths.temporary.c_str())) {
    fs->DeleteFile(paths.temporary.c_str());
    Trace::Error("CONFIG: Theme temp did not persist completely");
    return false;
  }

  if (targetExisted) {
    if (fs->exists(paths.backup.c_str()) &&
        !fs->DeleteFile(paths.backup.c_str())) {
      fs->DeleteFile(paths.temporary.c_str());
      return false;
    }
    if (!fs->MoveFile(paths.target.c_str(), paths.backup.c_str())) {
      fs->DeleteFile(paths.temporary.c_str());
      return false;
    }
  }

  if (!fs->MoveFile(paths.temporary.c_str(), paths.target.c_str())) {
    if (targetExisted && !RestoreThemeBackup(*fs, paths))
      Trace::Error("CONFIG: Could not roll back theme overwrite");
    fs->DeleteFile(paths.temporary.c_str());
    return false;
  }

  if (targetExisted && fs->exists(paths.backup.c_str()) &&
      !fs->DeleteFile(paths.backup.c_str())) {
    Trace::Error("CONFIG: Theme backup cleanup deferred");
  }
  Trace::Log("CONFIG", "Successfully exported theme to: %s",
             paths.target.c_str());
  return true;
}

bool Config::ImportTheme(const char *themeName, bool *loaded) {
  if (loaded != nullptr)
    *loaded = false;
  FileSystem *fs = FileSystem::GetInstance();
  if (fs == nullptr || themeName == nullptr)
    return false;

  const std::size_t suppliedLength = strlen(themeName);
  const char *extension = std::strrchr(themeName, '.');
  const bool hasThemeExtension =
      extension != nullptr && std::strcmp(extension, THEME_FILE_EXTENSION) == 0;
  const std::size_t baseLength = hasThemeExtension
                                     ? static_cast<std::size_t>(extension - themeName)
                                     : extension != nullptr
                                           ? static_cast<std::size_t>(extension - themeName)
                                           : suppliedLength;
  if (baseLength == 0U || baseLength > MAX_THEME_NAME_LENGTH ||
      suppliedLength > MAX_THEME_NAME_LENGTH +
                           strlen(THEME_FILE_EXTENSION) ||
      std::strchr(themeName, '/') != nullptr ||
      std::strchr(themeName, '\\') != nullptr) {
    return false;
  }

  // Extract the theme name without extension for storing in the config
  etl::string<MAX_THEME_NAME_LENGTH> baseThemeName = themeName;
  if (extension != nullptr) {
    // Remove the extension from the theme name
    baseThemeName =
        etl::string<MAX_THEME_NAME_LENGTH>(themeName, extension - themeName);
  }

  ThemeExportPaths journalPaths;
  if (!BuildThemeExportPaths(baseThemeName.c_str(), journalPaths)) {
    return false;
  }

  // Extension filtering belongs to the browser only. Import authenticity is
  // decided by the NPT root magic, schema version and complete semantic-role
  // set. This also rejects an old PTT merely renamed to .npt.
  etl::string<ThemeExportPaths::BasePathCapacity> importPath;
  if (extension == nullptr || hasThemeExtension) {
    if (!RecoverThemeExportJournal(*fs, journalPaths))
      return false;
    importPath = journalPaths.target;
  } else {
    importPath = THEMES_DIR;
    importPath.append("/");
    importPath.append(themeName);
    if (importPath.is_truncated())
      return false;
  }

  // Sanity check if the file exists
  if (!fs->exists(importPath.c_str())) {
    Trace::Error("Theme file does not exist: %s", importPath.c_str());
    return false;
  }

  // Create a persistency document from the file
  PersistencyDocument doc;
  if (!doc.Load(importPath.c_str())) {
    Trace::Error("Failed to load theme document: %s",
                 importPath.c_str());
    return false;
  }

  // Use the LoadTheme method to load the theme data
  ThemeLoadState staged;
  if (!ParseThemeDocument(doc, staged))
    return false;

  // Parsing, EOF validation and all fixed-capacity range checks complete
  // before this single live-state commit. A malformed theme therefore cannot
  // partially change the font, legacy colors, semantic palette or name.
  ApplyThemeState(*this, staged);
  if (Variable *themeVar = FindVariable(FourCC::VarThemeName))
    themeVar->SetString(baseThemeName.c_str());
  if (loaded != nullptr)
    *loaded = true;

  // Loading updates the live palette before persistence, so a failed Sync
  // must be observable by the caller. Returning only LoadTheme's result made
  // the browser close and claim success even though the selected theme would
  // disappear on reboot.
  return Save();
}
