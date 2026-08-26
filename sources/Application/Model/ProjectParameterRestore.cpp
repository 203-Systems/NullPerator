/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#include "ProjectParameterRestore.h"

#include "Application/Persistency/PersistencyAttribute.h"
#include "Application/Persistency/PersistencyDocument.h"

#include <climits>
#include <cstring>

namespace {

bool RestoreError(PersistencyDocument *document) {
  if (document != nullptr)
    document->MarkError();
  return false;
}

bool IsPersistedChoice(Variable &variable, const char *value) {
  if (value == nullptr || variable.GetType() != Variable::CHAR_LIST)
    return false;
  const char *const *choices = variable.GetListPointer();
  for (std::uint8_t index = 0U; index < variable.GetListSize(); ++index) {
    if (choices[index] != nullptr && strcasecmp(choices[index], value) == 0)
      return true;
  }
  return false;
}

bool IsVersionSuffixCharacter(char value) {
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || value == '-' || value == '_' ||
         value == '.';
}

} // namespace

bool ParseProjectVersionHundredthsForRestore(const char *text, int &result) {
  if (text == nullptr || text[0] == '\0')
    return false;
  const char *cursor = text;
  int whole = 0;
  bool gotWhole = false;
  while (*cursor >= '0' && *cursor <= '9') {
    gotWhole = true;
    const int digit = *cursor++ - '0';
    if (whole > (INT_MAX / 100 - digit) / 10)
      return false;
    whole = whole * 10 + digit;
  }
  if (!gotWhole)
    return false;

  int fraction = 0;
  int fractionDigits = 0;
  if (*cursor == '.') {
    ++cursor;
    if (*cursor < '0' || *cursor > '9')
      return false;
    while (*cursor >= '0' && *cursor <= '9') {
      if (fractionDigits < 2)
        fraction = fraction * 10 + (*cursor - '0');
      ++fractionDigits;
      ++cursor;
    }
  }
  if (fractionDigits == 0)
    fraction = 0;
  else if (fractionDigits == 1)
    fraction *= 10;

  if (*cursor != '\0') {
    if (*cursor++ != '-' || *cursor == '\0')
      return false;
    while (*cursor != '\0') {
      if (!IsVersionSuffixCharacter(*cursor++))
        return false;
    }
  }
  if (whole > (INT_MAX - fraction) / 100)
    return false;
  result = whole * 100 + fraction;
  return true;
}

bool StageProjectParameterRestore(PersistencyDocument *document,
                                  void *resolverContext,
                                  ProjectParameterResolver resolver,
                                  ProjectParameterRestorePacket &packet) {
  packet = {};
  if (document == nullptr || resolver == nullptr || document->HadError())
    return RestoreError(document);

  ProjectParameterRestorePacket staged{};
  // Draining PROJECT attributes can stop on the first child start when the XML
  // has no intervening whitespace. Do not advance past that child.
  bool element = document->r_ == YXML_ELEMSTART;
  if (!element)
    element = document->FirstChild();
  if (!element) {
    if (document->HadError() || document->r_ != YXML_ELEMEND)
      return RestoreError(document);
    packet = staged;
    return true;
  }

  while (element) {
    if (std::strcmp(document->ElemName(), "PARAMETER") != 0)
      return RestoreError(document);

    std::array<char, MAX_VARIABLE_STRING_LENGTH + 1U> name{};
    std::array<char, MAX_VARIABLE_STRING_LENGTH + 1U> value{};
    bool hasName = false;
    bool hasValue = false;
    while (document->NextAttribute()) {
      if (std::memchr(document->attrname_, '\0',
                      sizeof(document->attrname_)) == nullptr)
        return RestoreError(document);
      if (std::strcmp(document->attrname_, "NAME") == 0) {
        if (hasName || !CopyPersistedVariableAttribute(
                           *document, name.data(), name.size(), false))
          return RestoreError(document);
        hasName = true;
      } else if (std::strcmp(document->attrname_, "VALUE") == 0) {
        if (hasValue || !CopyPersistedVariableAttribute(
                            *document, value.data(), value.size(), true))
          return RestoreError(document);
        hasValue = true;
      }
    }
    if (document->HadError() || document->r_ != YXML_ELEMEND || !hasName ||
        !hasValue)
      return RestoreError(document);

    if (Variable *target = resolver(resolverContext, name.data())) {
      if (staged.count >= staged.updates.size())
        return RestoreError(document);
      ProjectParameterUpdate &update = staged.updates[staged.count++];
      update.target = target;
      update.value = value;
    }

    element = document->NextSibling();
  }

  // NextSibling must have consumed the closing PROJECT tag. Reaching raw EOF
  // with a positive parser state is truncated XML, not a successful payload.
  if (document->HadError() || document->r_ != YXML_ELEMEND)
    return RestoreError(document);
  packet = staged;
  return true;
}

bool ValidateProjectParameterRestorePacket(
    const ProjectParameterRestorePacket &packet, int minimumTempo,
    int maximumTempo) {
  if (minimumTempo > maximumTempo)
    return false;

  for (std::uint8_t index = 0U; index < packet.count; ++index) {
    const ProjectParameterUpdate &update = packet.updates[index];
    if (update.target == nullptr)
      return false;
    const char *value = update.value.data();
    int parsed = 0;
    const FourCC id = update.target->GetID();
    bool valid = false;

    if (id == FourCC::VarTempo) {
      valid = ParsePersistedIntegerAttribute(value, minimumTempo, maximumTempo,
                                             parsed);
    } else if (id == FourCC::VarMasterVolume) {
      valid = ParsePersistedIntegerAttribute(value, 0, 100, parsed);
    } else if (id == FourCC::VarChannel1Volume ||
               id == FourCC::VarChannel2Volume ||
               id == FourCC::VarChannel3Volume ||
               id == FourCC::VarChannel4Volume ||
               id == FourCC::VarChannel5Volume ||
               id == FourCC::VarChannel6Volume ||
               id == FourCC::VarChannel7Volume ||
               id == FourCC::VarChannel8Volume ||
               id == FourCC::VarPreviewVolume) {
      valid = ParsePersistedIntegerAttribute(value, 0, 99, parsed);
    } else if (id == FourCC::VarTranspose) {
      valid = ParsePersistedIntegerAttribute(value, -48, 48, parsed);
    } else if (id == FourCC::VarWrap) {
      valid = strcasecmp(value, "true") == 0 ||
              strcasecmp(value, "false") == 0;
    } else if (id == FourCC::VarScale || id == FourCC::VarScaleRoot) {
      valid = IsPersistedChoice(*update.target, value);
    } else {
      // The directory-derived project name used to be serialized. Keep that
      // bounded legacy field readable, but the caller intentionally ignores
      // it during commit.
      valid = id == FourCC::VarProjectName;
    }
    if (!valid)
      return false;

    for (std::uint8_t previous = 0U; previous < index; ++previous) {
      if (packet.updates[previous].target == update.target)
        return false;
    }
  }
  return true;
}
