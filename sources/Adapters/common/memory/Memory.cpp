/* SPDX-License-Identifier: BSD-3-Clause */
#include "System/Memory/Memory.h"
#include <cstdlib>
void *PlatformMemory::AllocateBulk(std::size_t bytes) {
  return std::malloc(bytes);
}
void PlatformMemory::FreeBulk(void *memory) { std::free(memory); }
