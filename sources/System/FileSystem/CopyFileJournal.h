/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include <cstddef>
#include <cstring>

// CopyFile journals live beside their destination, but their leaf names must
// not overlap the user sample namespace.  PicoTracker sample names are capped
// at 24 bytes; even a one-byte destination produces a 25-byte encoded journal
// leaf, and the hex encoding never contains a misleading `.wav` substring.
// This also avoids treating historical `kick.wav.copy.tmp` files as private.
namespace FileCopyJournal {

inline constexpr char TempPrefix[] = ".picotracker-copy-temp-";
inline constexpr char BackupPrefix[] = ".picotracker-copy-backup-";

static_assert(sizeof(TempPrefix) - 1U + 2U > 24U);
static_assert(sizeof(BackupPrefix) - 1U + 2U > 24U);

inline constexpr const char *LeafName(const char *path) {
  if (path == nullptr)
    return nullptr;
  const char *leaf = path;
  for (const char *cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\')
      leaf = cursor + 1;
  }
  return leaf;
}

inline constexpr bool StartsWith(const char *text, const char *prefix) {
  if (text == nullptr || prefix == nullptr)
    return false;
  while (*prefix != '\0') {
    if (*text++ != *prefix++)
      return false;
  }
  return true;
}

inline constexpr bool IsReservedLeaf(const char *path) {
  const char *leaf = LeafName(path);
  if (leaf == nullptr)
    return false;
  return StartsWith(leaf, TempPrefix) || StartsWith(leaf, BackupPrefix);
}

// Returns the exact destination capacity, including the trailing NUL.  Zero
// denotes an invalid/overflowing path.
inline std::size_t SiblingPathCapacity(const char *destination,
                                       const char *prefix) {
  if (destination == nullptr || prefix == nullptr)
    return 0U;
  const char *leaf = LeafName(destination);
  if (leaf == nullptr || leaf[0] == '\0')
    return 0U;
  const std::size_t parentLength = static_cast<std::size_t>(leaf - destination);
  const std::size_t prefixLength = std::strlen(prefix);
  const std::size_t leafLength = std::strlen(leaf);
  if (leafLength >
      (static_cast<std::size_t>(-1) - parentLength - prefixLength - 1U) / 2U)
    return 0U;
  return parentLength + prefixLength + leafLength * 2U + 1U;
}

inline bool BuildSiblingPath(const char *destination, const char *prefix,
                             char *output, std::size_t outputCapacity) {
  const std::size_t required = SiblingPathCapacity(destination, prefix);
  if (required == 0U || output == nullptr || outputCapacity < required)
    return false;

  const char *leaf = LeafName(destination);
  const std::size_t parentLength = static_cast<std::size_t>(leaf - destination);
  const std::size_t prefixLength = std::strlen(prefix);
  std::memcpy(output, destination, parentLength);
  std::memcpy(output + parentLength, prefix, prefixLength);

  static constexpr char Hex[] = "0123456789ABCDEF";
  char *encoded = output + parentLength + prefixLength;
  for (const unsigned char *cursor =
           reinterpret_cast<const unsigned char *>(leaf);
       *cursor != '\0'; ++cursor) {
    *encoded++ = Hex[*cursor >> 4U];
    *encoded++ = Hex[*cursor & 0x0FU];
  }
  *encoded = '\0';
  return true;
}

} // namespace FileCopyJournal
