#pragma once

#include <stddef.h>

// Android uses ordinary process memory instead of firmware linker sections.
// Retain the alignment required by the shared audio buffers.
#define _MEMORY_SECTIONS_H_
#define section(...)
#define PICOTRACKER_FAST_DATA
#define PICOTRACKER_FAST_AUDIO_BUFFER __attribute__((aligned(32)))
