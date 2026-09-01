/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */

#include "InstrumentFileValidator.h"

#include "Application/Instruments/I_Instrument.h"
#include "Application/Model/ProjectVersion.h"
#include "Application/Model/Table.h"
#include "PersistenceConstants.h"
#include "PersistencyAttribute.h"
#include "PersistencyDocument.h"

#include <cctype>
#include <cstring>

namespace {

bool EqualsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr)
    return false;
  while (*left != '\0' && *right != '\0') {
    const auto lhs = static_cast<unsigned char>(*left++);
    const auto rhs = static_cast<unsigned char>(*right++);
    if (std::tolower(lhs) != std::tolower(rhs))
      return false;
  }
  return *left == '\0' && *right == '\0';
}

bool IsValidPersistedTableValue(const char *text) {
  if (text == nullptr || text[0] == '\0')
    return false;
  if (std::strcmp(text, "-1") == 0)
    return true;

  int value = 0;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const unsigned char character = static_cast<unsigned char>(*cursor);
    if (!std::isdigit(character))
      return false;
    value = value * 10 + (character - '0');
    if (!IsValidTableIndex(value))
      return false;
  }
  return IsValidTableIndex(value);
}

bool IsValidInstrumentType(const char *text) {
  for (int type = IT_SAMPLE; type < IT_LAST; ++type) {
    if (EqualsIgnoreCase(text, InstrumentTypeNames[type]))
      return true;
  }
  return false;
}

} // namespace

bool ValidateInstrumentFilePayload(const char *name) {
  PersistencyDocument doc;
  if (!doc.Load(name) || !doc.FirstChild() ||
      std::strcmp(doc.ElemName(), "INSTRUMENT") != 0)
    return false;

  // The envelope is part of the recovery trust boundary too. Without a
  // unique, recognized TYPE, a superficially well-formed destination could
  // be mistaken for the committed copy and cause its valid .bak sibling to be
  // deleted before import later rejects the file.
  bool hasType = false;
  bool hasVersion = false;
  bool hasFormat = false;
  bool hasSchema = false;
  bool hasCreatedWith = false;
  bool attribute = doc.NextAttribute();
  while (attribute) {
    if (EqualsIgnoreCase(doc.attrname_, "TYPE")) {
      if (hasType || !IsValidInstrumentType(doc.attrval_))
        return false;
      hasType = true;
    } else if (EqualsIgnoreCase(doc.attrname_, "VERSION")) {
      if (hasVersion)
        return false;
      hasVersion = true;
    } else if (EqualsIgnoreCase(doc.attrname_, "FORMAT")) {
      if (hasFormat ||
          std::strcmp(doc.attrval_, nullperator_project::Format) != 0)
        return false;
      hasFormat = true;
    } else if (EqualsIgnoreCase(doc.attrname_, "SCHEMA")) {
      if (hasSchema ||
          std::strcmp(doc.attrval_, nullperator_project::Schema) != 0)
        return false;
      hasSchema = true;
    } else if (EqualsIgnoreCase(doc.attrname_, "CREATED_WITH")) {
      if (hasCreatedWith || doc.attrval_[0] == '\0')
        return false;
      hasCreatedWith = true;
    } else if (EqualsIgnoreCase(doc.attrname_, "ID")) {
      // ID belongs to the project InstrumentBank envelope, never to a
      // standalone .pti file.
      return false;
    }
    attribute = doc.NextAttribute();
  }
  if (doc.HadError() || !hasType)
    return false;
  if (hasFormat) {
    if (!hasSchema || !hasCreatedWith || hasVersion)
      return false;
  } else if (hasSchema || hasCreatedWith) {
    return false;
  }

  bool foundParameter = false;
  // NextAttribute() may already have selected the first PARAM. Re-entering
  // FirstChild() in that state would skip it, accepting a malformed second
  // parameter as though it were the whole file.
  bool element = doc.r_ == YXML_ELEMSTART || doc.FirstChild();
  while (element) {
    if (std::strcmp(doc.ElemName(), "PARAM") != 0)
      return false;

    bool hasName = false;
    bool hasValue = false;
    char parameterName[MAX_VARIABLE_STRING_LENGTH + 1U]{};
    char parameterValue[MAX_VARIABLE_STRING_LENGTH + 1U]{};
    bool attribute = doc.NextAttribute();
    while (attribute) {
      if (std::strcmp(doc.attrname_, "NAME") == 0) {
        if (hasName || !CopyPersistedVariableAttribute(
                           doc, parameterName, sizeof(parameterName), false))
          return false;
        hasName = true;
      } else if (std::strcmp(doc.attrname_, "VALUE") == 0) {
        if (hasValue || !CopyPersistedVariableAttribute(
                            doc, parameterValue, sizeof(parameterValue), true))
          return false;
        hasValue = true;
      }
      attribute = doc.NextAttribute();
    }
    if (!hasName || !hasValue || doc.HadError())
      return false;
    // Every table-capable instrument serializes this parameter as "table".
    // Validate it before RestoreContent reaches Variable::SetString(), which
    // intentionally has no per-variable range metadata.
    if (EqualsIgnoreCase(parameterName, "table") &&
        !IsValidPersistedTableValue(parameterValue))
      return false;
    if (EqualsIgnoreCase(parameterName, "InstrumentName") &&
        std::strlen(parameterValue) > MAX_INSTRUMENT_NAME_LENGTH)
      return false;
    foundParameter = true;
    element = doc.NextSibling();
  }
  return foundParameter && !doc.HadError() && doc.Finish();
}
