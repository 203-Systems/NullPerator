/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 PicoTracker contributors
 */

#pragma once

#include "esp_heap_caps.h"

#include <cstddef>
#include <cstdlib>
#include <limits>

// STL-compatible allocator for cold Node data. Prefer PSRAM, but retain an
// internal-RAM fallback so a missing or exhausted PSRAM device does not make
// filesystem construction fail solely because std::allocator cannot report an
// allocation error in this exception-free firmware build.
template <typename T> class NodePsramAllocator {
public:
  using value_type = T;

  NodePsramAllocator() noexcept = default;

  template <typename U>
  NodePsramAllocator(const NodePsramAllocator<U> &) noexcept {}

  [[nodiscard]] T *allocate(std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
      std::abort();

    const std::size_t bytes = count * sizeof(T);
    void *storage =
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage == nullptr) {
      storage =
          heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (storage == nullptr)
      std::abort();
    return static_cast<T *>(storage);
  }

  void deallocate(T *storage, std::size_t) noexcept {
    heap_caps_free(storage);
  }

  template <typename U>
  friend constexpr bool operator==(const NodePsramAllocator &,
                                   const NodePsramAllocator<U> &) noexcept {
    return true;
  }
};
