/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Persistency/PersistencyDocument.h"
#include "Foundation/Variables/Variable.h"

#include <cstddef>
#include <climits>
#include <cstdint>
#include <cstring>

// PersistencyDocument accepts attributes up to 63 bytes, while Variable and
// instrument parameter storage is intentionally limited to 40. Keep that
// boundary in one allocation-free helper so Project and .pti restore cannot
// accidentally reintroduce an unchecked strcpy between differently sized
// buffers.
inline bool CopyPersistedVariableAttribute(
    const PersistencyDocument &document, char *destination,
    std::size_t destinationCapacity, bool allowEmpty) {
  if (destination == nullptr || destinationCapacity == 0U)
    return false;
  destination[0] = '\0';
  const void *terminator =
      std::memchr(document.attrval_, '\0', sizeof(document.attrval_));
  if (terminator == nullptr)
    return false;
  const std::size_t length = static_cast<const char *>(terminator) -
                             static_cast<const char *>(document.attrval_);
  if ((!allowEmpty && length == 0U) ||
      length > MAX_VARIABLE_STRING_LENGTH ||
      length >= destinationCapacity) {
    return false;
  }
  std::memcpy(destination, document.attrval_, length + 1U);
  return true;
}

// Variable::SetString intentionally accepts user-facing text and therefore
// uses atoi-style parsing. Persisted indexes are a trust boundary: accept only
// the canonical signed decimal form and prove the value is in range before a
// restore can expose it to fixed-size model arrays.
inline bool ParsePersistedIntegerAttribute(const char *text, int minimum,
                                           int maximum, int &result) {
  if (text == nullptr || text[0] == '\0' || minimum > maximum)
    return false;

  const char *cursor = text;
  bool negative = false;
  if (*cursor == '-') {
    negative = true;
    ++cursor;
    if (*cursor == '\0')
      return false;
  }

  int magnitude = 0;
  const int magnitudeLimit =
      negative ? (minimum == INT_MIN ? INT_MAX : -minimum) : maximum;
  for (; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9')
      return false;
    const int digit = *cursor - '0';
    if (magnitude > (magnitudeLimit - digit) / 10)
      return false;
    magnitude = magnitude * 10 + digit;
  }

  const int value = negative ? -magnitude : magnitude;
  if (value < minimum || value > maximum)
    return false;
  result = value;
  return true;
}

// strtoul is intentionally not used at the persistence boundary: its result
// width differs between the ESP32 and host/WebAssembly builds and unchecked
// casts can silently wrap. Parse the canonical decimal token against the
// caller's explicit uint32 bound instead.
inline bool ParsePersistedUnsignedIntegerAttribute(const char *text,
                                                   std::uint32_t maximum,
                                                   std::uint32_t &result) {
  if (text == nullptr || text[0] == '\0')
    return false;

  std::uint32_t value = 0U;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9')
      return false;
    const std::uint32_t digit = static_cast<std::uint32_t>(*cursor - '0');
    if (digit > maximum || value > (maximum - digit) / 10U)
      return false;
    value = value * 10U + digit;
  }
  result = value;
  return true;
}
