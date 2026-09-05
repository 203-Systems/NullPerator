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
namespace ThemeDocument {
constexpr std::array<FourCC::enum_type, 12U> kLegacyThemeColorIds{{
    FourCC::VarBGColor,
    FourCC::VarFGColor,
    FourCC::VarHI1Color,
    FourCC::VarHI2Color,
    FourCC::VarConsoleColor,
    FourCC::VarCursorColor,
    FourCC::VarInfoColor,
    FourCC::VarWarnColor,
    FourCC::VarErrorColor,
    FourCC::VarAccentColor,
    FourCC::VarAccentAltColor,
    FourCC::VarEmphasisColor,
}};

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

} // namespace ThemeDocument
