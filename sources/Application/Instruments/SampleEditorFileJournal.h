/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Sample edit generations are hidden siblings whose leaf is exactly as long
// as the source WAV leaf:
//
//   KICK.wav -> .KICK.w0 (working), .KICK.b0 (backup)
//   KICK.WAV -> .KICK.w7 (working), .KICK.b7 (backup)
//
// The final nibble preserves the extension's case. This mapping is injective,
// reversible during directory recovery, bounded by the original FAT-safe
// filename, and does not recursively hex-encode long names.
class SampleEditorFileJournal final {
public:
  enum class Generation : char { Working = 'w', Backup = 'b' };

  static bool BuildPath(const char *source, Generation generation,
                        char *destination, std::size_t capacity) {
    if (destination == nullptr || capacity == 0U)
      return false;
    destination[0] = '\0';
    if (source == nullptr || source[0] == '\0')
      return false;

    const std::size_t sourceLength = std::strlen(source);
    if (sourceLength + 1U > capacity)
      return false;
    const char *leaf = Leaf(source);
    const std::size_t parentLength = static_cast<std::size_t>(leaf - source);
    const std::size_t leafLength = sourceLength - parentLength;
    if (leafLength < 4U || leaf[leafLength - 4U] != '.' ||
        Lower(leaf[leafLength - 3U]) != 'w' ||
        Lower(leaf[leafLength - 2U]) != 'a' ||
        Lower(leaf[leafLength - 1U]) != 'v')
      return false;

    std::uint8_t caseMask = 0U;
    for (std::size_t index = 0U; index < 3U; ++index) {
      if (IsUpper(leaf[leafLength - 3U + index]))
        caseMask |= static_cast<std::uint8_t>(1U << index);
    }
    const std::size_t baseLength = leafLength - 4U;
    std::memcpy(destination, source, parentLength);
    destination[parentLength] = '.';
    std::memcpy(destination + parentLength + 1U, leaf, baseLength);
    const std::size_t suffix = parentLength + 1U + baseLength;
    destination[suffix] = '.';
    destination[suffix + 1U] = static_cast<char>(generation);
    destination[suffix + 2U] = static_cast<char>('0' + caseMask);
    destination[sourceLength] = '\0';
    return true;
  }

  static bool DecodeBackupPath(const char *backup, char *destination,
                               std::size_t capacity) {
    if (destination == nullptr || capacity == 0U)
      return false;
    destination[0] = '\0';
    if (backup == nullptr || backup[0] == '\0')
      return false;
    const std::size_t backupLength = std::strlen(backup);
    if (backupLength + 1U > capacity)
      return false;
    const char *leaf = Leaf(backup);
    const std::size_t parentLength = static_cast<std::size_t>(leaf - backup);
    const std::size_t leafLength = backupLength - parentLength;
    if (leafLength < 4U || leaf[0] != '.' || leaf[leafLength - 3U] != '.' ||
        leaf[leafLength - 2U] != static_cast<char>(Generation::Backup) ||
        leaf[leafLength - 1U] < '0' || leaf[leafLength - 1U] > '7')
      return false;

    const std::uint8_t caseMask =
        static_cast<std::uint8_t>(leaf[leafLength - 1U] - '0');
    const std::size_t baseLength = leafLength - 4U;
    std::memcpy(destination, backup, parentLength);
    std::memcpy(destination + parentLength, leaf + 1U, baseLength);
    const std::size_t extension = parentLength + baseLength;
    destination[extension] = '.';
    const char wav[3] = {'w', 'a', 'v'};
    for (std::size_t index = 0U; index < 3U; ++index) {
      destination[extension + 1U + index] =
          (caseMask & static_cast<std::uint8_t>(1U << index)) != 0U
              ? static_cast<char>(wav[index] - ('a' - 'A'))
              : wav[index];
    }
    destination[backupLength] = '\0';
    return true;
  }

private:
  static const char *Leaf(const char *path) {
    const char *leaf = path;
    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
      if (*cursor == '/' || *cursor == '\\')
        leaf = cursor + 1;
    }
    return leaf;
  }

  static constexpr char Lower(char value) {
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
  }

  static constexpr bool IsUpper(char value) {
    return value >= 'A' && value <= 'Z';
  }
};
