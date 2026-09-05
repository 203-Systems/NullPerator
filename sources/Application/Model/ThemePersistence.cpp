/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "Application/Persistency/PersistenceConstants.h"
#include "Application/Persistency/PersistencyDocument.h"
#include "Config.h"
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

#include "ThemeDocument.h"
#define NPT_VERSION_NUMBER 1
using namespace ThemeDocument;
namespace {
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

bool Config::IsValidThemeName(const char *themeName) {
  return IsSafeThemeName(themeName);
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
