/* SPDX-License-Identifier: BSD-3-Clause */
#include "System/Memory/Memory.h"
#include <esp_heap_caps.h>
void *PlatformMemory::AllocateBulk(std::size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void PlatformMemory::FreeBulk(void *memory) { heap_caps_free(memory); }
