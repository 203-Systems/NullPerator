#pragma once

#include <stddef.h>

// Mach-O does not accept the embedded firmware's ELF section names. Define
// the same public placement macros without the section qualifier, preserving
// alignment while keeping the shared firmware headers untouched.
#define _MEMORY_SECTIONS_H_
#define section(...)
#define PICOTRACKER_FAST_DATA
#define PICOTRACKER_FAST_AUDIO_BUFFER __attribute__((aligned(32)))
