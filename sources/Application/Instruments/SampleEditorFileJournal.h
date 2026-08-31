/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class FileSystem;

// Sample edit generations are hidden siblings whose leaf is exactly as long
// as the source WAV leaf:
//
//   KICK.wav -> .KICK.w0 (working), .KICK.o0 (operation), .KICK.b0 (backup)
//   KICK.WAV -> .KICK.w7 (working), .KICK.o7 (operation), .KICK.b7 (backup)
//
// The final nibble preserves the extension's case. This mapping is injective,
// reversible during directory recovery, bounded by the original FAT-safe
// filename, and does not recursively hex-encode long names.
class SampleEditorFileJournal final {
public:
  enum class Generation : char {
    Working = 'w',
    Operation = 'o',
    Backup = 'b'
  };

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
    return DecodePath(backup, Generation::Backup, destination, capacity);
  }

  static bool DecodeWorkingPath(const char *working, char *destination,
                                std::size_t capacity) {
    return DecodePath(working, Generation::Working, destination, capacity);
  }

  static bool DecodeOperationPath(const char *operation, char *destination,
                                  std::size_t capacity) {
    return DecodePath(operation, Generation::Operation, destination, capacity);
  }

  static bool ValidateWav(FileSystem &fileSystem, const char *path);
  static bool RecoverDestination(FileSystem &fileSystem,
                                 const char *destination);
  static bool RecoverCurrentDirectory(FileSystem &fileSystem);

private:
  static bool DecodePath(const char *journal, Generation generation,
                         char *destination, std::size_t capacity) {
    if (destination == nullptr || capacity == 0U)
      return false;
    destination[0] = '\0';
    if (journal == nullptr || journal[0] == '\0')
      return false;
    const std::size_t journalLength = std::strlen(journal);
    if (journalLength + 1U > capacity)
      return false;
    const char *leaf = Leaf(journal);
    const std::size_t parentLength = static_cast<std::size_t>(leaf - journal);
    const std::size_t leafLength = journalLength - parentLength;
    if (leafLength < 4U || leaf[0] != '.' || leaf[leafLength - 3U] != '.' ||
        leaf[leafLength - 2U] != static_cast<char>(generation) ||
        leaf[leafLength - 1U] < '0' || leaf[leafLength - 1U] > '7')
      return false;

    const std::uint8_t caseMask =
        static_cast<std::uint8_t>(leaf[leafLength - 1U] - '0');
    const std::size_t baseLength = leafLength - 4U;
    std::memcpy(destination, journal, parentLength);
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
    destination[journalLength] = '\0';
    return true;
  }

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
