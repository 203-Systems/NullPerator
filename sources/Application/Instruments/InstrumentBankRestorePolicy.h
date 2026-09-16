/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "Application/Instruments/I_Instrument.h"
#include "Application/Model/Song.h"

#include <array>
#include <cstddef>
#include <cstdint>

inline bool InstrumentBankTokenEquals(const char *left, const char *right) {
  if (left == nullptr || right == nullptr)
    return false;
  while (*left != '\0' && *right != '\0') {
    char lhs = *left++;
    char rhs = *right++;
    if (lhs >= 'a' && lhs <= 'z')
      lhs = static_cast<char>(lhs - 'a' + 'A');
    if (rhs >= 'a' && rhs <= 'z')
      rhs = static_cast<char>(rhs - 'a' + 'A');
    if (lhs != rhs)
      return false;
  }
  return *left == '\0' && *right == '\0';
}

inline bool DecodeInstrumentBankSlotId(const char *text, std::uint8_t &slot) {
  if (text == nullptr || text[0] == '\0' || text[1] == '\0' ||
      text[2] != '\0') {
    return false;
  }
  const auto nibble = [](char character, std::uint8_t &value) {
    if (character >= '0' && character <= '9') {
      value = static_cast<std::uint8_t>(character - '0');
      return true;
    }
    if (character >= 'A' && character <= 'F') {
      value = static_cast<std::uint8_t>(character - 'A' + 10);
      return true;
    }
    if (character >= 'a' && character <= 'f') {
      value = static_cast<std::uint8_t>(character - 'a' + 10);
      return true;
    }
    return false;
  };
  std::uint8_t high = 0U;
  std::uint8_t low = 0U;
  if (!nibble(text[0], high) || !nibble(text[1], low))
    return false;
  const std::uint8_t decoded = static_cast<std::uint8_t>((high << 4U) | low);
  if (decoded >= MAX_INSTRUMENT_COUNT)
    return false;
  slot = decoded;
  return true;
}

inline bool DecodeInstrumentBankType(const char *text, InstrumentType &type) {
  if (text == nullptr || text[0] == '\0')
    return false;
  for (int candidate = IT_SAMPLE; candidate < IT_LAST; ++candidate) {
    if (InstrumentBankTokenEquals(text, InstrumentTypeNames[candidate])) {
      type = static_cast<InstrumentType>(candidate);
      return true;
    }
  }
  return false;
}

// Validates logical slots before any candidate is committed to the visible
// bank. Types share the same 64 slots; allocation failures are handled by the
// bank's transactional replacement path, not by per-type quotas here.
class InstrumentBankRestorePolicy {
public:
  bool Reserve(std::uint8_t slot, InstrumentType type) {
    if (slot >= seen_.size() || type <= IT_NONE || type >= IT_LAST ||
        seen_[slot]) {
      return false;
    }
    seen_[slot] = true;
    return true;
  }

  [[nodiscard]] bool Seen(std::uint8_t slot) const {
    return slot < seen_.size() && seen_[slot];
  }

private:
  std::array<bool, MAX_INSTRUMENT_COUNT> seen_{};
};
